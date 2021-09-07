/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include <AppConfig.h>
#include <LcdPainter.h>
#include <WindowAppImpl.h>
#include <app-common/zap-generated/attributes/Accessors.h>
#include <app/clusters/window-covering-server/window-covering-server.h>
#include <app/server/OnboardingCodesUtil.h>
#include <lcd.h>
#include <lib/core/CHIPError.h>
#include <lib/mdns/Advertiser.h>
#include <lib/support/CodeUtils.h>
#include <platform/CHIPDeviceLayer.h>
#include <qrcodegen.h>
#include <sl_simple_button_instances.h>
#include <sl_simple_led_instances.h>
#include <sl_system_kernel.h>

#define APP_TASK_STACK_SIZE (4096)
#define APP_TASK_PRIORITY 2
#define APP_EVENT_QUEUE_SIZE 10
#define EXAMPLE_VENDOR_ID 0xcafe

#define LCD_ICON_TIMEOUT 1000

using namespace chip::app::Clusters::WindowCovering;
#define APP_STATE_LED &sl_led_led0
#define APP_ACTION_LED &sl_led_led1

//------------------------------------------------------------------------------
// Timers
//------------------------------------------------------------------------------

WindowAppImpl::Timer::Timer(const char * name, uint32_t timeoutInMs, Callback callback, void * context) :
    WindowApp::Timer(name, timeoutInMs, callback, context)
{
    mHandler = xTimerCreate(name,          // Just a text name, not used by the RTOS kernel
                            timeoutInMs,   // == default timer period (mS)
                            false,         // no timer reload (==one-shot)
                            (void *) this, // init timer id = app task obj context
                            TimerCallback  // timer callback handler
    );
    if (mHandler == NULL)
    {
        EFR32_LOG("Timer create failed");
        appError(CHIP_ERROR_INTERNAL);
    }
}

void WindowAppImpl::Timer::Start()
{
    if (xTimerIsTimerActive(mHandler))
    {
        Stop();
    }

    // Timer is not active
    if (xTimerStart(mHandler, 100) != pdPASS)
    {
        EFR32_LOG("Timer start() failed");
        appError(CHIP_ERROR_INTERNAL);
    }

    mIsActive = true;
}

void WindowAppImpl::Timer::IsrStart()
{
    portBASE_TYPE taskWoken = pdFALSE; // For FreeRTOS timer (below).
    // Start/restart the button debounce timer (Note ISR version of FreeRTOS
    // api call here).
    xTimerStartFromISR(mHandler, &taskWoken);
    if (taskWoken != pdFALSE)
    {
        taskYIELD();
    }
    mIsActive = true;
}

void WindowAppImpl::Timer::Stop()
{
    mIsActive = false;
    if (xTimerStop(mHandler, 0) == pdFAIL)
    {
        EFR32_LOG("Timer stop() failed");
        appError(CHIP_ERROR_INTERNAL);
    }
}

void WindowAppImpl::Timer::TimerCallback(TimerHandle_t xTimer)
{
    Timer * timer = (Timer *) pvTimerGetTimerID(xTimer);
    if (timer)
    {
        timer->Timeout();
    }
}

//------------------------------------------------------------------------------
// Main Task
//------------------------------------------------------------------------------

StackType_t sAppStack[APP_TASK_STACK_SIZE / sizeof(StackType_t)];
StaticTask_t sAppTaskStruct;

uint8_t sAppEventQueueBuffer[APP_EVENT_QUEUE_SIZE * sizeof(WindowApp::Event)];
StaticQueue_t sAppEventQueueStruct;

WindowAppImpl WindowAppImpl::sInstance;

WindowApp & WindowApp::Instance()
{
    return WindowAppImpl::sInstance;
}

WindowAppImpl::WindowAppImpl() : mIconTimer("Timer:icon", LCD_ICON_TIMEOUT, OnIconTimeout, this) {}

void WindowAppImpl::OnTaskCallback(void * parameter)
{
    sInstance.Run();
}

void WindowAppImpl::OnIconTimeout(WindowApp::Timer & timer)
{
    sInstance.mIcon = LcdIcon::None;
    sInstance.UpdateLCD();
}

CHIP_ERROR WindowAppImpl::Init()
{
    WindowApp::Init();

    // Initialize App Task
    mHandle = xTaskCreateStatic(OnTaskCallback, APP_TASK_NAME, ArraySize(sAppStack), NULL, 1, sAppStack, &sAppTaskStruct);
    if (NULL == mHandle)
    {
        EFR32_LOG("Failed to allocate app task");
        return CHIP_ERROR_NO_MEMORY;
    }

    // Initialize App Queue
    mQueue = xQueueCreateStatic(APP_EVENT_QUEUE_SIZE, sizeof(WindowApp::Event), sAppEventQueueBuffer, &sAppEventQueueStruct);
    if (NULL == mQueue)
    {
        EFR32_LOG("Failed to allocate app event queue");
        return CHIP_ERROR_NO_MEMORY;
    }

    // Initialize LEDs
    LEDWidget::InitGpio();
    mStatusLED.Init(APP_STATE_LED);
    mActionLED.Init(APP_ACTION_LED);

    // Print setup info on LCD if available
    UpdateLCD();

    return CHIP_NO_ERROR;
}

CHIP_ERROR WindowAppImpl::Start()
{
    EFR32_LOG("Starting FreeRTOS scheduler");
    sl_system_kernel_start();

    return CHIP_NO_ERROR;
}

void WindowAppImpl::Finish()
{
    WindowApp::Finish();
    chip::Platform::MemoryShutdown();
    // Should never get here.
    EFR32_LOG("vTaskStartScheduler() failed");
    appError(CHIP_ERROR_INTERNAL);
}

void WindowAppImpl::PostEvent(const WindowApp::Event & event)
{
    if (mQueue)
    {
        BaseType_t status;
        if (xPortIsInsideInterrupt())
        {
            BaseType_t higherPrioTaskWoken = pdFALSE;
            status                         = xQueueSendFromISR(mQueue, &event, &higherPrioTaskWoken);

#ifdef portYIELD_FROM_ISR
            portYIELD_FROM_ISR(higherPrioTaskWoken);
#elif portEND_SWITCHING_ISR // portYIELD_FROM_ISR or portEND_SWITCHING_ISR
            portEND_SWITCHING_ISR(higherPrioTaskWoken);
#else                       // portYIELD_FROM_ISR or portEND_SWITCHING_ISR
#error "Must have portYIELD_FROM_ISR or portEND_SWITCHING_ISR"
#endif // portYIELD_FROM_ISR or portEND_SWITCHING_ISR
        }
        else
        {
            status = xQueueSend(mQueue, &event, 1);
        }

        if (!status)
        {
            EFR32_LOG("Failed to post event to app task event queue");
        }
    }
}

void WindowAppImpl::ProcessEvents()
{
    WindowApp::Event event = EventId::None;

    BaseType_t received = xQueueReceive(mQueue, &event, pdMS_TO_TICKS(10));
    while (pdTRUE == received)
    {
        DispatchEvent(event);
        received = xQueueReceive(mQueue, &event, 0);
    }
}

WindowApp::Timer * WindowAppImpl::CreateTimer(const char * name, uint32_t timeoutInMs, WindowApp::Timer::Callback callback,
                                              void * context)
{
    return new Timer(name, timeoutInMs, callback, context);
}

WindowApp::Button * WindowAppImpl::CreateButton(WindowApp::Button::Id id, const char * name)
{
    return new Button(id, name);
}

void WindowAppImpl::DispatchEvent(const WindowApp::Event & event)
{
    WindowApp::DispatchEvent(event);
    switch (event.mId)
    {
    case EventId::ResetWarning:
        EFR32_LOG("Factory Reset Triggered. Release button within %ums to cancel.", LONG_PRESS_TIMEOUT);
        // Turn off all LEDs before starting blink to make sure blink is
        // co-ordinated.
        UpdateLEDs();
        break;
    case EventId::ResetCanceled:
        EFR32_LOG("Factory Reset has been Canceled");
        UpdateLEDs();
        break;
    case EventId::ProvisionedStateChanged:
        UpdateLEDs();
        UpdateLCD();
        break;
    case EventId::ConnectivityStateChanged:
    case EventId::BLEConnectionsChanged:
        UpdateLEDs();
        break;
    case EventId::CoverTypeChange:
    case EventId::LiftChanged:
    case EventId::TiltChanged:
        UpdateLCD();
        break;
    case EventId::CoverChange:
        mIconTimer.Start();
        mIcon = (GetCover().mEndpoint == 1) ? LcdIcon::One : LcdIcon::Two;
        UpdateLCD();
        break;
    case EventId::TiltModeChange:
        mIconTimer.Start();
        mIcon = mTiltMode ? LcdIcon::Tilt : LcdIcon::Lift;
        UpdateLCD();
        break;
    default:
        break;
    }
}

void WindowAppImpl::UpdateLEDs()
{
    Cover & cover = GetCover();
    if (mResetWarning)
    {
        mStatusLED.Set(false);
        mStatusLED.Blink(500);

        mActionLED.Set(false);
        mActionLED.Blink(500);
    }
    else
    {
        // Consider the system to be "fully connected" if it has service
        // connectivity
        if (mState.haveServiceConnectivity)
        {
            mStatusLED.Set(true);
        }
        else if (mState.isThreadProvisioned && mState.isThreadEnabled)
        {
            mStatusLED.Blink(950, 50);
        }
        else if (mState.haveBLEConnections)
        {
            mStatusLED.Blink(100, 100);
        }
        else
        {
            mStatusLED.Blink(50, 950);
        }

        // Action LED

        if (EventId::None != cover.mLiftAction || EventId::None != cover.mTiltAction)
        {
            mActionLED.Blink(100);
        }
        else if (IsOpen(cover.mEndpoint))
        {
            mActionLED.Set(true);
        }
        else if (IsClosed(cover.mEndpoint))
        {
            mActionLED.Set(false);
        }
        else
        {
            mActionLED.Blink(1000);
        }
    }
}

void WindowAppImpl::UpdateLCD()
{
    // Update LCD
#ifdef DISPLAY_ENABLED
    if (mState.isThreadProvisioned)
    {
        Cover & cover      = GetCover();
        EmberAfWcType type = TypeGet(cover.mEndpoint);
        uint16_t lift      = 0;
        uint16_t tilt      = 0;
        Attributes::GetCurrentPositionLift(cover.mEndpoint, &lift);
        Attributes::GetCurrentPositionTilt(cover.mEndpoint, &tilt);
        LcdPainter::Paint(type, static_cast<uint8_t>(lift), static_cast<uint8_t>(tilt), mIcon);
    }
    else
    {
        LCDWriteQRCode((uint8_t *) mQRCode.c_str());
    }
#endif
}

void WindowAppImpl::OnMainLoop()
{
    mStatusLED.Animate();
    mActionLED.Animate();
}

//------------------------------------------------------------------------------
// Buttons
//------------------------------------------------------------------------------
WindowAppImpl::Button::Button(WindowApp::Button::Id id, const char * name) : WindowApp::Button(id, name) {}

void WindowAppImpl::OnButtonChange(const sl_button_t * handle)
{
    WindowApp::Button * btn = static_cast<Button *>((handle == &sl_button_btn0) ? sInstance.mButtonUp : sInstance.mButtonDown);

    if (sl_button_get_state(handle) == SL_SIMPLE_BUTTON_PRESSED)
    {
        btn->Press();
    }
    else
    {
        btn->Release();
    }
}

// Silabs button callback from button event ISR
void sl_button_on_change(const sl_button_t * handle)
{
    WindowAppImpl * app = static_cast<WindowAppImpl *>(&WindowAppImpl::sInstance);
    app->OnButtonChange(handle);
}
#pragma once

#include <Arduino.h>
#include <functional>

// Button timing configuration
#define BUTTON_DEBOUNCE_TIME_MS    50      // Debounce time in ms
#define BUTTON_CLICK_TIMEOUT_MS    500     // Max time between clicks for multi-click
#define BUTTON_LONG_PRESS_TIME_MS  1200    // Long press fires after this long held (no need to release)
#define BUTTON_VERY_LONG_PRESS_TIME_MS 5000 // Very long press fires after this long held
#define BUTTON_READ_INTERVAL_MS    10      // How often to read the button

class Button {
public:
    enum EventType {
        NONE,
        SHORT_PRESS,
        DOUBLE_PRESS,
        TRIPLE_PRESS,
        QUADRUPLE_PRESS,
        LONG_PRESS,
        VERY_LONG_PRESS,
        ANY_PRESS
    };

    using EventCallback = std::function<void()>;
    using RawUpCallback = std::function<void(uint32_t held_ms)>;

    Button(uint8_t pin, bool activeState = LOW);
    Button(uint8_t pin, bool activeState, bool isAnalog, uint16_t analogThreshold = 20);
    
    void begin();
    void update();
    
    // Set callbacks for different events
    void onShortPress(EventCallback callback) { _onShortPress = callback; }
    void onDoublePress(EventCallback callback) { _onDoublePress = callback; }
    void onTriplePress(EventCallback callback) { _onTriplePress = callback; }
    void onQuadruplePress(EventCallback callback) { _onQuadruplePress = callback; }
    void onLongPress(EventCallback callback) { _onLongPress = callback; }
    void onVeryLongPress(EventCallback callback) { _onVeryLongPress = callback; }
    void onAnyPress(EventCallback callback) { _onAnyPress = callback; }

    // Raw mode (used for Morse keying): no click counting. Every press calls onRawDown,
    // every release calls onRawUp with the hold time -- unless a long press already fired.
    // Long / very long press still fire as normal.
    void setRawMode(bool raw) { _rawMode = raw; _clickCount = 0; if (_state == WAITING_FOR_MULTI_CLICK) _state = IDLE; }
    bool isRawMode() const { return _rawMode; }
    void onRawDown(EventCallback callback) { _onRawDown = callback; }
    void onRawUp(RawUpCallback callback) { _onRawUp = callback; }
    
    // State getters
    bool isPressed() const { return _currentState; }
    EventType getLastEvent() const { return _lastEvent; }

private:
    enum State {
        IDLE,
        PRESSED,
        RELEASED,
        WAITING_FOR_MULTI_CLICK
    };

    uint8_t _pin;
    bool _activeState;
    bool _isAnalog;
    uint16_t _analogThreshold;
    
    State _state = IDLE;
    bool _currentState;
    bool _lastState;
    
    uint32_t _stateChangeTime = 0;
    uint32_t _pressTime = 0;
    uint32_t _releaseTime = 0;
    uint32_t _lastReadTime = 0;
    
    uint8_t _clickCount = 0;
    EventType _lastEvent = NONE;
    
    // Callbacks
    EventCallback _onShortPress = nullptr;
    EventCallback _onDoublePress = nullptr;
    EventCallback _onTriplePress = nullptr;
    EventCallback _onQuadruplePress = nullptr;
    EventCallback _onLongPress = nullptr;
    EventCallback _onVeryLongPress = nullptr;
    EventCallback _onAnyPress = nullptr;
    EventCallback _onRawDown = nullptr;
    RawUpCallback _onRawUp = nullptr;
    bool _rawMode = false;
    bool _longFired = false;
    
    bool readButton();
    void handleStateChange();
    void triggerEvent(EventType event);
};
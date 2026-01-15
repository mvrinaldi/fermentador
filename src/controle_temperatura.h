// controle_temperatura.h
#pragma once

struct DetailedControlStatus {
    bool coolerActive;
    bool heaterActive;
    float estimatedPeak;
    bool peakDetection;
    String stateName;
    bool isWaiting;
    uint16_t waitTimeRemaining;
    String waitReason;
};
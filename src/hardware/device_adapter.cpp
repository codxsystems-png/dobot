#include "hardware/device_adapter.h"

namespace hardware {
// IDeviceAdapter is an abstract base class, 
// but Q_OBJECT requires the MOC output to be compiled.
// This empty file ensures CMake's AUTOMOC processes it correctly.
}

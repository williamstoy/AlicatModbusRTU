// reference: https://documents.alicat.com//manuals/DOC-MANUAL-MPL.pdf
// also reference: https://documents.alicat.com/manuals/ModbusRTU_Manual.pdf



#include <Arduino.h>
#include <ModbusInterface.h>
#include <AlicatMODBUSRTU.h>



AlicatMODBUSRTU::AlicatMODBUSRTU(int modbusID, int deviceType, ModbusInterface& modbus, HardwareSerial& serial, bool verbose) 
: _modbusID(modbusID), _modbus(modbus), _serial(serial), _verbose(verbose), _deviceType(deviceType)
{
  _registerOffset       = -1; // default register offset. Apparently this is a MODBUS Standard
}



/** 
 * HELPER FUNCTIONS
*/
void AlicatMODBUSRTU::setRegisterOffset(int registerOffset) {
    _registerOffset = registerOffset;
}



void AlicatMODBUSRTU::setModbusID(int modbusID) {
    _modbusID = modbusID;
}



int AlicatMODBUSRTU::offsetRegister(int address) {
    return address + _registerOffset;
}



void AlicatMODBUSRTU::getDeviceStatisticRegisterAddress(int statisticIndex, int *registerAddress) {
  if (statisticIndex < 1 || statisticIndex > 20) {
    if (_verbose) _serial.println("ERROR: function:'getDeviceStatisticRegisterAddress', argument statisticIndex is out of bounds");

    return;
  }

  *registerAddress = REGISTER_DEVICE_STATISTIC_1_VALUE + 2*(statisticIndex - 1);
}



/**
 * READ AND WRITE REGISTER FUNCTIONS
*/
void AlicatMODBUSRTU::readSingleRegister(int registerAddress, uint16_t *registerValue) {
  const int dataLength = 1;
  uint16_t response[dataLength];

  if (!_modbus.readHoldingRegisterValues(_modbusID, offsetRegister(registerAddress), dataLength, response)) {
      _serial.print("ERROR: Failed to read register: ");
      _serial.println(registerAddress);
      return;
  }

  // concatenate the two response bytes into a single integer and return
  *registerValue = response[0]; // @todo: get rid of this (response[1] << 8) + response[0];
}



void AlicatMODBUSRTU::readRegistersAsFloat(int registerAddress, float *floatValue) {
  const int dataLength = 2;
  uint16_t response[dataLength];

  union {
    float asFloat;
    uint16_t asBytes[dataLength];
  } floatValueUnion;

  if (!_modbus.readHoldingRegisterValues(_modbusID, offsetRegister(registerAddress), dataLength, response)) {
      _serial.print("ERROR: Failed to read register: ");
      _serial.println(registerAddress);
      return;
  }

  // concatenate the response bytes into a single float using the following format:
  // All 32-bit values are handled in consecutive Modbus registers in big-
  // endian format. This means bits 31:16 are in the lower numbered Modbus
  // register and bits 15:0 are in the higher register. All floating-point values
  // are IEEE 32-bit floats.

  floatValueUnion.asBytes[1] = response[0];
  floatValueUnion.asBytes[0] = response[1];

  *floatValue = floatValueUnion.asFloat;
}



void AlicatMODBUSRTU::writeRegistersAsFloat(int registerAddress, float floatValue) {
  const int dataLength = 2;
  
  union {
    float asFloat;
    uint16_t asBytes[dataLength];
  } floatValueUnion;

  floatValueUnion.asFloat = floatValue;

  uint16_t data[dataLength];
  for(int i = 0; i < dataLength; i++) {
    data[i] = floatValueUnion.asBytes[dataLength - i - 1];
  }

  _modbus.writeHoldingRegisterValues(_modbusID, offsetRegister(registerAddress), data, dataLength);
}



void AlicatMODBUSRTU::writeSingleRegister(int registerAddress, uint16_t registerValue) {
  const int dataLength = 1;
  uint16_t registerValueArray[dataLength] = { registerValue };

  _modbus.writeHoldingRegisterValues(_modbusID, offsetRegister(registerAddress), registerValueArray, dataLength);
}


/**
 * MODBUS READING AND STATUS REGISTERS
*/
void AlicatMODBUSRTU::setSetpoint(float setpoint) {
  if (!deviceIsController()) {
    if (_verbose) _serial.println("ERROR: function, 'setSetpoint' is not used for devices of this type");

    return;
  }

  writeRegistersAsFloat(REGISTER_SETPOINT, setpoint);
}



void AlicatMODBUSRTU::getSetpoint(float *setPoint) {
  if (!deviceIsController()) {
    if (_verbose) _serial.println("ERROR: function, 'setSetpoint' is not used for devices of this type");

    return;
  }

  int registerAddress;

  if (deviceIsMassFlow()) {
    getDeviceStatisticRegisterAddress(5, &registerAddress);
  } else if (deviceIsPressureController()) {
    getDeviceStatisticRegisterAddress(2, &registerAddress);
  }

  readRegistersAsFloat(registerAddress, setPoint);
}



void AlicatMODBUSRTU::getPressure(float *pressure) {
  // all devices have a pressure statistic

  int registerAddress;
  
  getDeviceStatisticRegisterAddress(1, &registerAddress);

  readRegistersAsFloat(registerAddress, pressure);
}



void AlicatMODBUSRTU::setMixtureGasProperties(int mixtureIndex, uint16_t gasIndex, float gasPercent) {
  if (!deviceIsMassFlow()) {
    if (_verbose) _serial.println("ERROR: function, 'setMixtureGasProperties' is not used for devices of this type");

    return;
  }

  // check to make sure the mixture index is between 1 and 5
  if (mixtureIndex < 1 || mixtureIndex > 5) {
    if (_verbose) _serial.println("ERROR: function, 'setMixtureGasProperties', mixtureIndex must be between 1 and 5");

    return;
  }

  // check to make sure the gas index is between 0 and 210
  if (gasIndex < 0 || gasIndex > 210) {
    if (_verbose) _serial.println("ERROR: function, 'setMixtureGasProperties', gasIndex must be between 0 and 210");

    return;
  }

  // check to make sure the gas percent is between 0 and 100
  if (gasPercent < 0.0 || gasPercent > 100.0) {
    if (_verbose) _serial.println("ERROR: function, 'setMixtureGasProperties', gasPercent must be between 0 and 100");

    return;
  }

  int gasIndexRegisterAddress    = REGISTER_MIXTURE_GAS_1_INDEX + 2*(mixtureIndex - 1);
  int gasPercentRegisterAddress  = gasIndexRegisterAddress + 1;

  // write the gas index
  writeSingleRegister(gasIndexRegisterAddress, gasIndex);

  // write the gas percent
  // "...to specify a mix of 50%, a value of 5000 is written into the gas percentage register."
  uint16_t gasPercentInt = (uint16_t)round(gasPercent * 100.0);

  writeSingleRegister(gasPercentRegisterAddress, gasPercentInt);
}



void AlicatMODBUSRTU::getMixtureGasProperties(int mixtureIndex, uint16_t *gasIndex, float *gasPercent) {
  if (!deviceIsMassFlow()) {
    if (_verbose) _serial.println("ERROR: function, 'getMixtureGasProperties' is not used for devices of this type");

    return;
  }

  // check to make sure the mixture index is between 1 and 5
  if (mixtureIndex < 1 || mixtureIndex > 5) {
    if (_verbose) _serial.println("ERROR: function, 'getMixtureGasProperties', mixtureIndex must be between 1 and 5");

    return;
  }

  int gasIndexRegisterAddress    = REGISTER_MIXTURE_GAS_1_INDEX + (mixtureIndex - 1);
  int gasPercentRegisterAddress  = gasIndexRegisterAddress + 1;

  // read the gas index
  readSingleRegister(gasIndexRegisterAddress, gasIndex);

  // read the gas percent
  uint16_t gasPercentageInt;
  readSingleRegister(gasPercentRegisterAddress, &gasPercentageInt);

  *gasPercent = ((float)gasPercentageInt) / 100.0;
}



void AlicatMODBUSRTU::setGasNumber(uint16_t gasIndex) {
  if (!deviceIsMassFlow()) {
    if (_verbose) _serial.println("ERROR: function, 'setGasNumber' is not used for devices of this type");

    return;
  }

  // check to make sure the gas index is between 0 and 210
  if (gasIndex < 0 || gasIndex > 210) {
    if (_verbose) _serial.println("ERROR: function, 'setGasNumber', gasIndex must be between 0 and 210");

    return;
  }

  writeSingleRegister(REGISTER_GAS_NUMBER, gasIndex);
}



void AlicatMODBUSRTU::getGasNumber(uint16_t *gasIndex) {
  if (!deviceIsMassFlow()) {
    if (_verbose) _serial.println("ERROR: function, 'getGasNumber' is not used for devices of this type");

    return;
  }

  readSingleRegister(REGISTER_GAS_NUMBER, gasIndex);
}



void AlicatMODBUSRTU::getStatusFlags() {
    uint16_t status;
    readSingleRegister(REGISTER_DEVICE_STATUS, &status);

    _status.TEMPERATURE_OVERFLOW          = status & STATUS_BIT_TEMPERATURE_OVERFLOW;
    _status.TEMPERATURE_UNDERFLOW         = status & STATUS_BIT_TEMPERATURE_UNDERFLOW;
    _status.VOLUMETRIC_OVERFLOW           = status & STATUS_BIT_VOLUMETRIC_OVERFLOW;
    _status.VOLUMETRIC_UNDERFLOW          = status & STATUS_BIT_VOLUMETRIC_UNDERFLOW;
    _status.MASS_OVERFLOW                 = status & STATUS_BIT_MASS_OVERFLOW;
    _status.MASS_UNDERFLOW                = status & STATUS_BIT_MASS_UNDERFLOW;
    _status.PRESSURE_OVERFLOW             = status & STATUS_BIT_PRESSURE_OVERFLOW;
    _status.TOTALIZER_OVERFLOW            = status & STATUS_BIT_TOTALIZER_OVERFLOW;
    _status.PID_LOOP_IN_HOLD              = status & STATUS_BIT_PID_LOOP_IN_HOLD;
    _status.ADC_ERROR                     = status & STATUS_BIT_ADC_ERROR;
    _status.PID_EXHAUST                   = status & STATUS_BIT_PID_EXHAUST;
    _status.OVER_PRESSURE_LIMIT           = status & STATUS_BIT_OVER_PRESSURE_LIMIT;
    _status.FLOW_OVERFLOW_DURING_TOTALIZE = status & STATUS_BIT_FLOW_OVERFLOW_DURING_TOTALIZE;
    _status.MEASUREMENT_ABORTED           = status & STATUS_BIT_MEASUREMENT_ABORTED;
    _status.ANY_ERROR                     = status > 0;

    if (_verbose) {
        _serial.print("STATUS Bits: ");
        _serial.println(status, BIN);

        if (_status.TEMPERATURE_OVERFLOW)           _serial.println("STATUS: TEMPERATURE OVERFLOW bit is set");
        if (_status.TEMPERATURE_UNDERFLOW)          _serial.println("STATUS: TEMPERATURE UNDERFLOW bit is set");
        if (_status.VOLUMETRIC_OVERFLOW)            _serial.println("STATUS: VOLUMETRIC OVERFLOW bit is set");
        if (_status.VOLUMETRIC_UNDERFLOW)           _serial.println("STATUS: VOLUMETRIC UNDERFLOW bit is set");
        if (_status.MASS_OVERFLOW)                  _serial.println("STATUS: MASS OVERFLOW bit is set");
        if (_status.MASS_UNDERFLOW)                 _serial.println("STATUS: MASS UNDERFLOW bit is set");
        if (_status.PRESSURE_OVERFLOW)              _serial.println("STATUS: PRESSURE OVERFLOW bit is set");
        if (_status.TOTALIZER_OVERFLOW)             _serial.println("STATUS: TOTALIZER OVERFLOW bit is set");
        if (_status.PID_LOOP_IN_HOLD)               _serial.println("STATUS: PID LOOP IN HOLD bit is set");
        if (_status.ADC_ERROR)                      _serial.println("STATUS: ADC ERROR bit is set");
        if (_status.PID_EXHAUST)                    _serial.println("STATUS: PID EXHAUST bit is set");
        if (_status.OVER_PRESSURE_LIMIT)            _serial.println("STATUS: OVER PRESSURE LIMIT bit is set");
        if (_status.FLOW_OVERFLOW_DURING_TOTALIZE)  _serial.println("STATUS: FLOW OVERFLOW DURING TOTALIZE bit is set");
        if (_status.MEASUREMENT_ABORTED)            _serial.println("STATUS: MEASUREMENT ABORTED bit is set");
    }
}



void AlicatMODBUSRTU::getFlowTemperature(float *flowTemperature) {
  if (!deviceIsMassFlow() && !deviceIsLiquid()) {
    if (_verbose) _serial.println("ERROR: function, 'getFlowTemperature' is not used for devices of this type");

    return;
  }

  int registerAddress;
  
  getDeviceStatisticRegisterAddress(2, &registerAddress);

  readRegistersAsFloat(registerAddress, flowTemperature);
}



void AlicatMODBUSRTU::getVolumetricFlow(float *volumetricFlow) {
  if (!deviceIsMassFlow() && !deviceIsLiquid()) {
    if (_verbose) _serial.println("ERROR: function, 'getVolumetricFlow' is not used for devices of this type");

    return;
  }

  int registerAddress;
  
  getDeviceStatisticRegisterAddress(3, &registerAddress);

  readRegistersAsFloat(registerAddress, volumetricFlow);
}



void AlicatMODBUSRTU::getMassFlow(float *massFlow) {
  if (!deviceIsMassFlow()) {
    if (_verbose) _serial.println("ERROR: function, 'getMassFlow' is not used for devices of this type");

    return;
  }

  int registerAddress;
  
  getDeviceStatisticRegisterAddress(4, &registerAddress);

  readRegistersAsFloat(registerAddress, massFlow);
}



void AlicatMODBUSRTU::getMassTotal(float *massTotal) {
  if (!deviceIsMassFlow()) {
    if (_verbose) _serial.println("ERROR: function, 'getMassFlow' is not used for devices of this type");

    return;
  }

  int registerAddress;

  if (deviceIsController()) {
    getDeviceStatisticRegisterAddress(6, &registerAddress);
  } else {
    getDeviceStatisticRegisterAddress(5, &registerAddress);
  }

  readRegistersAsFloat(registerAddress, massTotal);
}



/**
 * SPECIAL COMMANDS
*/
bool AlicatMODBUSRTU::sendSpecialCommand(uint16_t command, uint16_t argument) {
  uint16_t data[2] = { command, argument };

  _modbus.writeHoldingRegisterValues(_modbusID, offsetRegister(REGISTER_COMMAND_ID), data, 2);

  uint16_t status;
  readSingleRegister(REGISTER_COMMAND_ARGUMENT, &status);

  return handleSpecialCommandStatusCode(status);
}



bool AlicatMODBUSRTU::handleSpecialCommandStatusCode(uint16_t status) {  
  switch(status) {
    case STATUS_CODE_SUCCESS:
      //_serial.println("SUCCESSFUL STATUS CODE RETURNED");
      return true;
      break;
    case STATUS_CODE_INVALID_COMMAND_ID:
      if (_verbose) _serial.println("ERROR: INVALID COMMAND ID");
      break;
    case STATUS_CODE_INVALID_SETTING:
      if (_verbose) _serial.println("ERROR: INVALID SETTING");
      break;
    case STATUS_CODE_REQUESTED_FEATURE_IS_UNSUPPORTED:
      if (_verbose)  _serial.println("ERROR: REQUESTED FEATURE IS UNSUPPORTED");
      break;
    case STATUS_CODE_INVALID_GAS_MIX_INDEX:
      if (_verbose) _serial.println("ERROR: INVALID GAS MIX INDEX (MASS FLOW DEVICE)");
      break;
    case STATUS_CODE_INVALID_GAS_MIX_CONSTITUENT:
      if (_verbose) _serial.println("ERROR: INVALID GAS MIX CONSTITUENT (MASS FLOW DEVICE)");
      break;
    case STATUS_CODE_INVALID_GAS_MIX_PERCENTAGE:
      if (_verbose) _serial.println("ERROR: INVALID GAS MIX PERCENTAGE (MASS FLOW DEVICE)");
      break;
    default:
      if (_verbose) _serial.println("ERROR: UNKNOWN STATUS CODE");
      break;
  }

  return false;
}



void AlicatMODBUSRTU::readPIDValue(uint16_t coefficientID, uint16_t *coefficientValue) {
  if (!deviceIsController()) {
    if (_verbose) _serial.println("ERROR: function, 'readPIDValue' is not used for devices of this type");

    return;
  }

  if (coefficientID < 0 || coefficientID > 2) {
    if (_verbose) _serial.println("ERROR: function:'readPIDValue', argument coefficientID is out of bounds");

    return;
  }

  sendSpecialCommand(SPECIAL_COMMAND_READ_PID_VALUE, coefficientID);

  readSingleRegister(REGISTER_COMMAND_ARGUMENT, coefficientValue);
}



void AlicatMODBUSRTU::changeGasNumber(uint16_t gasTableIndex) {
  if (!deviceIsMassFlow()) {
    if (_verbose) _serial.println("ERROR: function, 'changeGasNumber' is not used for devices of this type");

    return;
  }

  sendSpecialCommand(SPECIAL_COMMAND_CHANGE_GAS_NUMBER, gasTableIndex);
}



void AlicatMODBUSRTU::createCustomGasMixture(uint16_t gasMixtureIndex) {
  if (!deviceIsMassFlow()) {
    if (_verbose) _serial.println("ERROR: function, 'createCustomGasMixture' is not used for devices of this type");
    
    return;
  }

  if (gasMixtureIndex != 0 && (gasMixtureIndex < 236 || gasMixtureIndex > 255)) {
    if (_verbose) _serial.println("ERROR: function:'createCustomGasMixture', argument gasMixtureIndex is out of bounds");

    return;
  }

  sendSpecialCommand(SPECIAL_COMMAND_CREATE_CUSTOM_GAS_MIXTURE, gasMixtureIndex);
}



void AlicatMODBUSRTU::deleteCustomGasMixture(uint16_t gasMixtureIndex) {
  if (!deviceIsMassFlow()) {
    if (_verbose) _serial.println("ERROR: function, 'deleteCustomGasMixture' is not used for devices of this type");
    
    return;
  }

  sendSpecialCommand(SPECIAL_COMMAND_DELETE_CUSTOM_GAS_MIXTURE, gasMixtureIndex);
}



void AlicatMODBUSRTU::tare(uint16_t tareArgument) {
  bool proceed = true;

  switch(tareArgument) {
    case 0:
      if(!deviceIsPressureController()) proceed = false;
      break;
    case 1:
      if(!deviceIsPressureController()) proceed = false;
      break;
    case 2:
      if(!deviceIsMassFlow() && !deviceIsLiquid()) proceed = false;
      break;
    default:
      proceed = false;
      break;
  }

  if (!proceed) {
    if (_verbose) {
      _serial.print("ERROR: Incorrect Tare Argument ");
      _serial.print(tareArgument);
      _serial.print(" for device of type");
      _serial.println(_deviceType);
    }

    return;
  }

  sendSpecialCommand(SPECIAL_COMMAND_TARE, tareArgument);
}



/**
 * DEVICE TYPE CHECK
*/
bool AlicatMODBUSRTU::deviceIsMassFlow() {
  return _deviceType == DEVICE_TYPE_MASS_FLOW_METER || _deviceType == DEVICE_TYPE_MASS_FLOW_CONTROLLER;
}



bool AlicatMODBUSRTU::deviceIsController() {
  return _deviceType == DEVICE_TYPE_PSID_CONTROLLER || _deviceType == DEVICE_TYPE_GAUGE_PRESSURE_CONTROLLER || _deviceType == DEVICE_TYPE_MASS_FLOW_CONTROLLER;
}



bool AlicatMODBUSRTU::deviceIsPressureController() {
  return _deviceType == DEVICE_TYPE_PSID_CONTROLLER || _deviceType == DEVICE_TYPE_GAUGE_PRESSURE_CONTROLLER;
}



bool AlicatMODBUSRTU::deviceIsLiquid() {
  return _deviceType == DEVICE_TYPE_LIQUID_CONTROLLER;
}



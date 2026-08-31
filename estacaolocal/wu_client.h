// Upload para o Weather Underground (protocolo PWS "updateraw").

#ifndef WU_CLIENT_H
#define WU_CLIENT_H

#include <Arduino.h>

#include "readings.h"

namespace WuClient {

// Publica as leituras. Devolve true quando o WU responde "success".
bool publish(const Readings& r);

}  // namespace WuClient

#endif  // WU_CLIENT_H

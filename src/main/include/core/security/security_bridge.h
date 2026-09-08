#pragma once

#include "core/config/configuration.h"
#include "rqp/security/security_settings.h"

SecuritySettings security_settings_from(const ReqPackConfig& config);
void wire_reqpack_security_runtime();

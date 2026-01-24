// ispindel_envio.h - Processamento e envio de dados iSpindel
// ✅ REFATORADO: Envio MySQL movido para mysql_sender.cpp
#pragma once

#include "ispindel_struct.h"

// Processa e envia dados do iSpindel para Brewfather e MySQL
void processCloudUpdatesiSpindel();
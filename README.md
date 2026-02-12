# Documentação da ECU ESP32-S3

Pasta de referencia do projeto: `C:\Users\Pedro\Desktop\ESP32-EFI`

Este repositório contém o firmware e a documentação da ECU baseada em **ESP32-S3** (arquitetura single-chip).

## Estrutura

- `firmware/s3` - Firmware principal da ECU (controle de motor + supervisão)
- `docs/development` - Documentação técnica de arquitetura e módulos
- `docs/testing` - Estratégia e planos de validação
- `tools` - Scripts de build e suporte

## Módulos principais

1. `sync` - sincronismo angular (CKP/CMP)
2. `engine_control` - lógica de combustível/ignição e malha fechada
3. `sensor_processing` - aquisição e filtragem de sensores
4. `safety_monitor` - limites e modo de segurança
5. `twai_lambda` - leitura de lambda via CAN (TWAI)

## Como usar

1. Configure o ESP-IDF (`v5.5.1+`)
2. Build: `tools/build_all.ps1` (PowerShell) ou `tools/build_all.bat` (CMD)
3. Flash: `cd firmware/s3 && idf.py -p PORT flash`

## Status

- Alvo ativo: **ESP32-S3**
- Build validado localmente em `firmware/s3` em **11/02/2026**
- EOIT tuning and CAN runtime calibration: see docs/eoit_calibration.md

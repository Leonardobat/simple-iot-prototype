# iot_device — Smart Home (DAM 2026/27)

Firmware C (ESP-IDF v6.1) para **ESP32-S3** (16 MB flash, 8 MB PSRAM, `/dev/ttyACM0`). Lê sensores, publica no
Firebase Realtime Database (REST/HTTPS) e escuta o nó do dispositivo por SSE. A SPA nunca fala com o ESP32 diretamente.
README.md tem o detalhe; regras de código em `.claude/rules/` (MISRA, camadas, testes, Doxygen). Não há CI.

## Estrutura

- `main/iot_device.c`: arranca módulos e amostra a cada 5 s (grava na flash); o `reading_uploader` envia.
- `components/`: `connectivity_provider` (NVS, Wi-Fi, SNTP, em segundo plano; `is_connected`/`is_time_valid`), `firebase_auth` (ID token), `http_client` (PUT + SSE),
  `firebase_paths`, `firebase_rtdb`, `data_repository` (lotes de leituras), `flash_fs` (HAL LittleFS, partição `storage` de 4 MB), `reading_store` (log de leituras na flash), `config_store` (`enabled` na flash), `reading_uploader` (task de envio, 1 s), `sensor_provider` (catálogo; reais: `temperature_1` pelo
  `chip_temperature`, `temperature_2` por um LM35 no GPIO5, `luminosity_1` por um LDR no GPIO4 e `gas_1` por um MQ-135 no GPIO8 (via divisor), todos via `analog_input`; `luminosity_2` simulado), `actuator_controller` (catálogo,
  estado e saídas: `light_1`/`light_2` no GPIO6/7, `rgb_light_1` no LED RGB, `curtain_1` em dois relés GPIO18/21, `relay_1`/`relay_2`), `rgb_led`, `gpio_output`,
  `chip_temperature` e `analog_input` (HALs), `device_listener` (stream), `json_writer`, `sse_parser`, `backoff`, `utils` (helpers partilhados: `utils_snprintf`, `utils_bool_text`).
- `test/fakes/`: fakes dos HALs (`http_client`, `flash_fs`, `rgb_led`, `gpio_output`, `chip_temperature`, `analog_input`).
- `test/<componente>/`: testes Unity no QEMU (só os HALs são fakes); `test/integration/` só na placa.

## Comandos

```sh
source ~/.espressif/tools/activate_idf_v6.1.sh   # em cada terminal
idf.py build
idf.py -p /dev/ttyACM0 flash monitor             # Ctrl+] sai
```

- Testes unitários: em `test/<componente>/`, no QEMU esp32s3 (Claude pode executar).
- Testes de integração na placa: só o usuário executa (BOOT/RESET manual), com `make test-integration` (pytest-embedded, ADR 0006).

## Dados e configuração

- ADRs em `docs/adr/` (0001 modelo de dados, 0002 atuadores, 0003 sensores, 0004 persistência offline (leituras e `enabled` na flash), 0005 dispositivos do projeto, 0006 pytest-embedded na integração). Modelo: catálogo `devices/<DEVICE_ID>` (ESP: type/unit/simulated via PATCH;
  SPA: enabled/command; o ESP escuta este nó), leituras `PATCH readings/<DEVICE_ID>` (até 5 por pedido, chave `<timestamp_ms>`), `home/` só da SPA.
- Auth email/senha (ID token de 1 h, renovado 5 min antes). Config via `idf.py menuconfig`; `sdkconfig.defaults` é versionado.
- **Nunca** colocar senha, API key ou token no código, em logs ou em `sdkconfig.defaults`; `sdkconfig` está no `.gitignore`.
- Stack de 8 KB é necessária para o handshake TLS (task principal, `reading_uploader`, `device_listener`).
- A partição `storage` (LittleFS, 4 MB) guarda leituras e `config.bin`; `partitions.csv` tem um só slot de app (sem OTA).

## Pendente

Testar a ADR 0004 na placa (integração: Wi-Fi a cair, reset offline). Regras de segurança do Realtime Database; sensores externos reais (opcional).

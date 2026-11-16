# Arquitetura, modularidade e tratamento de erros


Camadas (dependência só de cima para baixo; nunca o contrário):

```
main/ (orquestração)  →  lógica de negócio  →  drivers de periféricos  →  HAL (esp_driver_*, wrappers finos)
```

- **Negócio**: sem includes de `driver/*`, `esp_wifi`, `freertos` direto quando evitável; recebe dependências por interface.
- **Drivers**: falam com o periférico via HAL; sem regras de negócio.
- **HAL**: wrappers finos sobre ESP-IDF; é o **único** ponto que será mockado nos testes. Hoje os HALs são
  `http_client` (rede, sobre `esp_http_client`), `rgb_led` (sobre `espressif/led_strip`), `gpio_output`
  (sobre `driver/gpio`), `chip_temperature` (sobre `driver/temperature_sensor`) e `analog_input`
  (sobre o ADC one-shot + calibração do `esp_adc`); os fakes ficam em `test/fakes/<hal>/`.
- Cada componente em `components/<nome>/` com `include/<nome>.h` (API pública), `src/`,
  `private_include/<nome>_config.h` (constantes privadas), `CMakeLists.txt`, `README` curto se necessário.
- **Encapsulamento**: estado de módulo em tipo opaco no header (`typedef struct sse_parser_s *sse_parser_handle_t;`),
  definição no `.c`; instâncias vêm de pool estático (sem `malloc`). Tipos de **dados** copiados por valor
  (ex.: `sensor_readings_t`) podem ser `struct` pública documentada.
  Funções internas `static`; só o necessário é exportado. Prefixe símbolos públicos com o nome do módulo (`sensor_*`).
- **Substituição em teste por link-time**: o projeto de teste inclui um componente fake com o **mesmo nome** do HAL
  (`test/fakes/http_client`), sem `#ifdef`; as camadas acima são as reais. Tabela de funções na `*_init` só se um
  componente precisar de duas implementações em runtime.
- **Ciclo de vida**: todo `*_init` que adquire recurso (mutex, task, cliente, timer) tem `*_deinit` simétrico,
  seguro em init parcial; se o `*_init` falha a meio, chama o próprio `*_deinit` (sem `goto`). Init duplo e uso sem
  init são contratos (`assert`).
- Sem variáveis globais públicas; sem dependências circulares entre componentes; `REQUIRES` mínimos no CMake.
- Headers com include guards `#ifndef <NOME>_H` (não `#pragma once`), autossuficientes e sem definições de variáveis.

### Tratamento de erros

- Toda função falível retorna `esp_err_t` (ou enum de erro do módulo). Resultado em parâmetro de saída (`out`), nunca em sentinelas.
- **Nunca ignorar retorno**: verifique, propague ou trate. Use `ESP_RETURN_ON_ERROR`/`ESP_GOTO_ON_ERROR`;
  `ESP_ERROR_CHECK` só em boot/inicialização irrecuperável. Cast `(void)` apenas com comentário do porquê.
- Mensagens com `ESP_LOGx(TAG, ...)` (`TAG` `static const char *` por arquivo); nunca logar segredos (senha, token, API key).
- Operações de rede/IO têm **timeout** e política de retry limitada (`MAX_RETRIES`, backoff); nada de espera infinita sem watchdog.
  Locks com timeout (`ESP_ERR_TIMEOUT`). Reconexões que nunca devem desistir (Wi-Fi, stream) usam backoff exponencial
  com teto (componente `backoff`).
- Falha parcial deve deixar o módulo em estado consistente (sem vazar recursos).

### Programação defensiva

- **Contratos internos** (bug do programador): `assert()` / `configASSERT` — ponteiro nulo, módulo não inicializado, índice inválido.
- **Dados externos** (sensores, rede, JSON do Firebase, config): validação em tempo de execução com retorno
  `ESP_ERR_INVALID_ARG`/`ESP_ERR_INVALID_SIZE`/`ESP_ERR_INVALID_RESPONSE`; cheque faixa, tamanho, NaN e *null-termination*.
- Parse de JSON/HTTP: limite de tamanho, checagem de cada campo e tipo; nunca confiar no payload.
- Inicialize todas as variáveis na declaração; valide `size` antes de qualquer `memcpy`.


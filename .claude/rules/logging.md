# Logging (esp_log, Log V2)

Referência: [ESP-IDF v6.1 — Logging library](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/system/log.html).

### Biblioteca e versão

- Todo log do projeto usa a biblioteca **`esp_log`** (`#include "esp_log.h"`), recomendada pela Espressif.
  **Proibido** `printf`/`puts`/`putchar`/`fprintf`/`ets_printf`/`esp_rom_printf` nos componentes e no `main`
  (a única saída fora do `esp_log` é a do próprio Unity nos testes).
- **Log V2** obrigatório: `CONFIG_LOG_VERSION_2=y` em `sdkconfig.defaults` (produto e integração) e em
  `test/common/sdkconfig.defaults` (unitários). Não reverter para V1: o V2 guarda só a string de formato do
  usuário na flash (binário menor), formata num único handler (`esp_log`) e só lê o timestamp quando o log sai.
- O V2 usa **um pouco mais de stack**: toda task que loga precisa de margem
  (validar com `uxTaskGetStackHighWaterMark` ao criar ou alterar tasks).

### Uso

- Um `static const char *TAG = "<componente>";` por arquivo `.c`, com o nome do componente (ex.: `"firebase_auth"`).
  Não usar tags dinâmicas (o cache de nível por tag compara ponteiros).
- Macros por contexto:
  - `ESP_LOGE/W/I/D/V` — código normal (tasks).
  - `ESP_EARLY_LOGx` — só antes do scheduler/heap (startup).
  - `ESP_DRAM_LOGx` — só com interrupções desligadas ou cache da flash inacessível; tag com `DRAM_STR("tag")`.
  - Em ISR, evitar logar; se indispensável, `ESP_DRAM_LOGx`.
- Os macros `ESP_RETURN_ON_*`/`ESP_GOTO_ON_*` já registram com `ESP_LOGE`: não duplicar a mensagem logo antes/depois.
- Buffers: `ESP_LOG_BUFFER_HEX_LEVEL` / `ESP_LOG_BUFFER_CHAR_LEVEL` (`esp_log_buffer.h`), nunca laços de `ESP_LOGx`.

### Níveis

| Nível | Quando                                                                 |
|-------|------------------------------------------------------------------------|
| `E`   | Falha que o módulo não recupera sozinho (requisição falhou, init falhou) |
| `W`   | Problema tratado/mitigado (reconexão, retry, evento descartado)        |
| `I`   | Marcos da operação normal (conectou, login, publicação periódica ≤ 1 Hz) |
| `D`   | Diagnóstico detalhado (valores intermediários, eventos frequentes > 1 Hz) |
| `V`   | Rastreamento muito verboso (bytes, estados internos)                   |

- Para depurar um arquivo, defina `LOG_LOCAL_LEVEL` **antes** de `#include "esp_log.h"` (ou no `CMakeLists.txt`
  do componente); não deixar níveis elevados commitados. Ajuste em runtime com `esp_log_level_set(TAG, ...)`.

### Segurança

- **Nunca** logar segredos: senha, token, API key, `?auth=`/`?key=` de URLs (logar só até o `?`, ex.: `"%.*s"`).
  Para configuração sensível, logar apenas o tamanho (`"password %u chars"`).
- Respostas de erro de serviços externos podem ser logadas (sem credenciais) para diagnóstico.

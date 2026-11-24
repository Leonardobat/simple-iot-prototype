# ADR 0006 — Testes de integração na placa com pytest-embedded

- **Estado:** aceite
- **Data:** 2026-11-16
- **Âmbito:** testes de integração (`test/integration/`) e, desde a emenda de 2026-11-16, também os unitários no
  QEMU (`test/test_unit.py`). Complementa a [ADR 0004](0004-persistencia-offline.md), cujos testes precisam de reset.

## Contexto

Os testes de integração correm hoje com `idf.py flash monitor` e um humano a ler o log:

- **Todos os testes correm no mesmo arranque**, um a seguir ao outro. Quando um falha antes do seu `deinit`
  (o LM35 fora da faixa, em 2026-11-16), o seguinte rebenta no `assert` de "já inicializado" e o log mostra o
  crash em vez da causa.
- **Não há relatório por teste** nem um código de saída que um script possa usar.
- **A ADR 0004 não se testa assim:** a persistência de leituras e de `enabled` só se prova **depois de um
  reset**, e o envio depois de uma queda de rede exige esperar pelo estado, não por um tempo fixo.

## Decisão

Os testes de integração passam a correr com o **pytest-embedded** (serviços `esp` e `idf`).

- **Menu do Unity:** o projeto de integração usa o componente `test/common/unity_menu_main`, cujo `app_main`
  chama `unity_run_menu()`. Cada teste corre por escolha do pytest. Os unitários mantêm o `unity_main`
  (corre tudo e imprime o resumo, sem ler o stdin).
- **Consola no USB Serial/JTAG:** o pytest fala com a placa pela porta USB nativa (`/dev/ttyACM0`). O
  `test/integration/sdkconfig.defaults` põe `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`, porque com a consola na UART0
  (o padrão do produto) o `Enter` do pytest nunca chega ao stdin, o menu fica a girar e o watchdog dispara.
  Também desliga o `CONFIG_ESP_TASK_WDT_INIT`, como os unitários.
- **Nenhum caso pode ficar por correr:** o pytest-embedded regista como `IGNORE` um caso que não conseguiu iniciar
  (placa que não responde depois de um reset, porta USB que caiu), e isso não reprova a execução. O
  `test_integration.py` exige um `PASS` para cada caso esperado.
- **Um reset antes de cada teste:** cada `TEST_CASE` arranca do zero. Cada um chama o seu `ensure_online()`,
  nada fica de um teste para o seguinte, e uma falha não contamina os outros.
- **Testes com reset a meio:** usam `TEST_CASE_MULTIPLE_STAGES`. O fim de cada fase chama `esp_restart()` e o
  pytest dá a fase seguinte depois do arranque.
- **Grupo `[integration]`:** o `test_integration.py` corre todos os testes desse grupo com
  `run_all_single_board_cases`. Qualquer falha reprova o pytest (`Unity test failed`) e sai um relatório por teste.
- **Limpeza no fim:** `test_integration_cleanup_deletes_test_nodes` é o último do ficheiro, por isso corre
  mesmo que os anteriores falhem. Apaga os nós do Firebase do `DEVICE_ID` de teste **e a partição `storage`**.
- **Unitários (emenda):** `test/test_unit.py` tem um caso por projeto de `test/<componente>/`, com `-s` (log ao vivo). O
  `run_unit_tests.sh` só compila cada projeto e chama o pytest. Os HALs que o QEMU não emula (`rgb_led`, `analog_input`)
  continuam sem projeto próprio.
- **Comando:** `make test-integration` compila, grava a placa e corre o pytest. A porta tem de estar livre
  (sem `monitor` aberto). Só o usuário o executa (BOOT/RESET manual), como já estava nas regras de teste.

### Os testes novos da ADR 0004

| Teste | O que prova |
|-------|-------------|
| `test_integration_reading_store_and_config_survive_a_reset` (2 fases) | Na LittleFS real: o `config.bin` e as leituras gravadas antes do `esp_restart()` estão lá depois dele |
| `test_integration_uploader_sends_stored_readings_after_wifi_drop` | Com a rede cortada (`esp_wifi_disconnect()`), as leituras ficam na flash; quando o Wi-Fi volta, o uploader esvazia a fila, e cada leitura só sai da fila depois de um 2xx |

O arranque sem rede e a amostragem sem relógio ficam nos unitários (QEMU com fakes): na placa exigiriam
desligar o router a meio do teste.

## Alternativas consideradas

| Alternativa | Porque não |
|-------------|------------|
| Manter `idf.py flash monitor` e ler o log | Sem relatório por teste, sem reset entre testes e sem testes com reset a meio. |
| Script serial próprio (pyserial) | Reescreveria o que o pytest-embedded já faz: flash, menu do Unity, reset, timeouts e relatório JUnit. |
| Manter o `run_unit_tests.sh` com o laço em bash para o QEMU | Tinha sido adiado por problemas com uma versão antiga do pytest-embedded. Com a 2.9.3 e o QEMU 9.2.2 do IDF 6.1 os 16 projetos correm em ~8 s, com relatório por teste. Emenda de 2026-11-16: os unitários passam também para o pytest. |
| Um único `TEST_CASE` com tudo | Perde o relatório por teste e o reset a cada um. |

## Consequências

- **Dependências de Python no venv do IDF:** `pytest-embedded`, `pytest-embedded-serial-esp`,
  `pytest-embedded-idf` e `pytest-embedded-qemu` (2.9.x). O IDF 6.1 não as instala sozinho; o `README` diz como (`pip install`).
- **Cada teste paga o Wi-Fi e o SNTP** (cerca de 5 a 10 s) se precisar de rede. É o preço do isolamento.
- **A partição `storage` da placa de integração é apagada** no fim, incluindo leituras ainda por enviar de uma
  execução anterior do produto. A placa de teste é a mesma do produto, por isso convém ter o produto sem
  leituras pendentes, ou aceitar essa perda.
- **Tudo o que um teste de integração precisa de reset passa a ter uma forma de o fazer** (multi-fase), o que
  destrava os próximos testes (por exemplo, o `enabled` restaurado no arranque).
- Fica fora do `make check`: precisa da placa.

# Testes (Unity + QEMU e hardware real)


### Unitários — Unity + QEMU (padrão, rápidos)

- Framework **Unity** (`unity.h`), um diretório por componente em `test/<componente>/` (ver `test/data_repository`);
  `app_main` compartilhado em `test/common/unity_main`, sdkconfig comum em `test/common/sdkconfig.defaults`.
- Rodar com `make test` (`test/run_unit_tests.sh`); falha se um projeto rodar 0 testes. pytest-embedded fica
  para uma fase futura (não funciona bem com QEMU).
- Rodam **no QEMU esp32s3**; a lógica testada não pode depender de hardware real. Um HAL que o QEMU emula
  (ex.: `gpio_output`) também tem projeto próprio; o `rgb_led` não, porque o QEMU não emula o RMT (o
  `led_strip` fica à espera do fim da transmissão), nem o `analog_input`, porque com o `esp_adc` ligado o
  arranque no QEMU fica preso na calibração do ADC — esses HALs são cobertos só pela integração na placa.
- Isole I2C, GPIO, Wi-Fi, HTTP, NVS físico etc. atrás do HAL e **mocke só o HAL** (fake manual em `test/fakes/`,
  ou CMock); as camadas acima entram reais no teste.
- Testar: caminho feliz, parâmetros inválidos, limites, falhas injetadas (timeout, erro de driver, payload corrompido) e *deinit*/reinit.
- Testes determinísticos, independentes entre si, sem `vTaskDelay` longo, sem rede; `setUp`/`tearDown` restauram o estado.
- Nome: `test_<modulo>_<cenario>_<resultado_esperado>`; use `TEST_ASSERT_EQUAL_*` com tipos de largura fixa.
- Todo bug corrigido ganha um teste de regressão.
- **Claude executa** os testes unitários no QEMU.

### Integração — hardware real (ESP32-S3)

- Cobrem periféricos reais, pilha Wi-Fi/TLS, Firebase real, desempenho de rádio e consumo.
- Ficam separados dos unitários (ex.: `test/<componente>/integration/` ou tag/grupo próprio) e **nunca** correm no QEMU.
- **Não deixam lixo no Firebase**: escrevem só sob o `DEVICE_ID` de teste (`integration-test`) e o último teste
  apaga cada nó escrito (PUT `null`), depois de confirmar que o `DEVICE_ID` é o de teste. Um teste novo que
  escreva noutro nó acrescenta esse nó à limpeza.
- **Executados manualmente pelo usuário** (`make test-integration`). Claude escreve/atualiza esses testes e
  descreve como rodá-los, mas **não** tenta executá-los.


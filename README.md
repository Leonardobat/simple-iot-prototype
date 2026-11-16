# iot_device — Smart Home (DAM 2026/27)

Firmware do **dispositivo IoT** do projeto *Controlo e Monitorização Inteligente de uma Smart Home*
(Desenvolvimento de Aplicações Multiplataforma, MEEE).

O projeto completo é composto por uma **SPA** (dashboard, regras de automação e gestão de divisões),
uma base de dados **Firebase Realtime Database** e este dispositivo IoT. Este repositório contém
apenas a parte IoT: um **ESP32-S3** que

- **lê os sensores** da casa (temperatura, humidade, luminosidade, movimento, ...);
- **processa** as leituras e as converte para o formato JSON da base de dados;
- **publica periodicamente** os dados no Firebase;
- **escuta o Firebase em tempo real** e aciona os **atuadores** (iluminação, estores, rega, ...)
  conforme o estado escrito pela SPA.

## Arquitetura

```
 ┌───────────┐        ┌────────────────────────────┐        ┌──────────────────┐
 │    SPA    │ ◄────► │ Firebase Realtime Database │ ◄────► │ ESP32-S3 (este   │ ◄──► sensores
 │ (browser) │        │                            │        │ repositório)     │ ◄──► atuadores
 └───────────┘        └────────────────────────────┘        └──────────────────┘
```

A SPA **nunca** comunica diretamente com o ESP32: toda a troca de dados passa pelo Firebase.
O ESP32 usa a API REST do Realtime Database sobre HTTPS:

| Direção          | Mecanismo                                          | Caminho no Firebase                   |
|------------------|----------------------------------------------------|---------------------------------------|
| ESP32 → Firebase | `PATCH` no arranque com o catálogo do dispositivo  | `devices/<DEVICE_ID>/{sensors,actuators}` |
| ESP32 → Firebase | `PATCH` a cada 1 s, com até 5 leituras guardadas na flash (amostradas a cada 5 s) e o estado dos atuadores | `readings/<DEVICE_ID>/<timestamp_ms>` |
| Firebase → ESP32 | Stream SSE (`Accept: text/event-stream`)           | `devices/<DEVICE_ID>`                 |

### Autenticação

O dispositivo autentica-se como um utilizador **email/senha** do Firebase Authentication:

1. No primeiro pedido, faz `POST` a `identitytoolkit.googleapis.com/v1/accounts:signInWithPassword`
   e recebe um **ID token** (JWT válido por 1 h).
2. Todos os pedidos ao Realtime Database levam `?auth=<ID token>`.
3. O token é renovado automaticamente (novo login) 5 min antes de expirar. Quando expira, o Firebase
   fecha o stream dos atuadores com `auth_revoked` e o dispositivo volta a ligar-se com o token novo.

### Modelo de dados

Decisões e alternativas em `docs/adr/`:
[0001 — modelo de dados](docs/adr/0001-modelo-de-dados.md) e
[0002 — atuadores, hardware e drivers](docs/adr/0002-atuadores.md) e
[0003 — sensores](docs/adr/0003-sensores.md) e
[0004 — persistência offline](docs/adr/0004-persistencia-offline.md) e
[0005 — dispositivos do projeto](docs/adr/0005-dispositivos-do-projeto.md) (duas ESP32-S3, mesmo firmware).

```json
{
  "devices": {
    "device-01": {
      "sensors": {
        "temperature_1": { "type": "temperature", "unit": "°C", "simulated": true, "enabled": true },
        "luminosity_1":  { "type": "luminosity", "unit": "lx", "simulated": false, "enabled": false }
      },
      "actuators": {
        "light_1":     { "type": "light", "simulated": false, "enabled": true, "command": true },
        "rgb_light_1": { "type": "rgb_light", "simulated": false, "enabled": true, "command": true, "color": "#ff8000" },
        "curtain_1":   { "type": "curtain", "simulated": false, "enabled": true, "command": "up" }
      }
    }
  },
  "readings": {
    "device-01": {
      "1762630000000": {
        "sensors":   { "temperature_1": 22.4 },
        "actuators": { "light_1": true }
      }
    }
  },
  "home": { "rooms": { "sala": { "name": "Sala", "sensors": { "device-01": { "temperature_1": true } } } } }
}
```

- `devices/<id>`: o ESP escreve `type`, `unit` e `simulated` (PATCH, não apaga o resto); a SPA
  escreve `enabled` e `command`. Em falta, valem `false`. O ESP escuta este nó e aplica cada
  alteração: sensores desativados não são publicados; um atuador só liga com `enabled` e `command`.
- `readings/<id>/<timestamp_ms>`: epoch em ms (relógio via SNTP). `actuators` é o estado real, que
  confirma o `command`. A SPA lê a última com `limitToLast(1)` + `onChildAdded`.
- `home/`: divisões e regras, só da SPA; o ESP nunca o lê.

## Estado atual

- [x] Ligação Wi-Fi com reconexão automática (backoff exponencial) e sincronização de hora (SNTP)
- [x] Cliente HTTPS para o Firebase (PUT keep-alive, POST, stream SSE cancelável com reconexão)
- [x] Autenticação email/senha no Firebase Auth, com renovação automática do token
- [x] Publicação das leituras a cada 5 s: `temperature_1` (sensor interno do ESP32-S3) e
      `temperature_2` (LM35), `luminosity_1` (LDR) e `gas_1` (MQ-135) **reais**, os restantes simulados
- [x] Catálogo de sensores e atuadores, ativação (`enabled`) e comandos via stream
- [x] Atuadores **reais**: `light_1`/`light_2` (LEDs no GPIO6/GPIO7), `rgb_light_1` (LED RGB da placa,
      com cor `"#rrggbb"`) e `curtain_1` (dois relés, GPIO18/GPIO21, com `up`/`down`/`stop`); `relay_1` e
      `relay_2` simulados
- [x] Um só firmware para as duas ESP32-S3 (sala; quarto e cozinha): ver a
      [ADR 0005](docs/adr/0005-dispositivos-do-projeto.md)
- [x] Modelo de dados para a SPA ([ADR 0001](docs/adr/0001-modelo-de-dados.md))
- [x] Sem `malloc` por request, `init`/`deinit` simétricos, 214 testes unitários (QEMU) + 14 de integração

## Estrutura do código

```
main/                          Ponto de entrada: arranca os módulos e amostra a cada 5 s (task principal)
components/<nome>/
  include/<nome>.h             API pública (Doxygen: pré/pós-condições, erros, thread-safety)
  private_include/<nome>_config.h   Timeouts, tamanhos e constantes do componente
  src/                         Implementação
components/
  connectivity_provider/       NVS, Wi-Fi (station), reconexão com backoff, SNTP, scan de diagnóstico
  sensor_provider/             Catálogo e enabled dos sensores, leitura (chip, LM35, LDRs, MQ-135, mocks) e JSON
  chip_temperature/            HAL do sensor de temperatura interno (driver/temperature_sensor)
  analog_input/                HAL de entradas analógicas (ADC1): ADC one-shot + calibração (esp_adc)
  actuator_controller/         Catálogo, enabled e command dos atuadores, aciona as saídas, estado real e JSON
  rgb_led/                     HAL do LED RGB endereçável (WS2812 por RMT, espressif/led_strip)
  gpio_output/                 HAL de saídas digitais simples (LEDs, relés), com nível inicial
  device_listener/             Task do stream de devices/<DEVICE_ID>: aplica put/patch, reconecta com backoff
  data_repository/             Grava lotes de leituras em readings/<DEVICE_ID> e o catálogo (PATCH)
  flash_fs/                    HAL do sistema de ficheiros da flash (LittleFS, partição `storage` de 4 MB)
  reading_store/               Log de leituras na flash (segmentos de 256 registos com CRC), oldest first
  config_store/                `enabled` dos sensores e atuadores, guardado na flash (sobrevive a resets)
  reading_uploader/            Task de envio (1 s): publica o catálogo e envia até 5 leituras por pedido
  firebase_paths/              Caminhos da base de dados (devices/..., readings/...)
  json_writer/                 Escreve objetos JSON planos num buffer, sem overflow
  utils/                       Helpers partilhados: `utils_snprintf` (avisa de truncagem) e `utils_bool_text`
  firebase_rtdb/               API REST do Realtime Database: <DB_URL>/<path>.json?auth=<token>
  firebase_auth/               Login email/senha no Firebase Auth e cache/renovação do ID token
  http_client/                 HAL de rede: transporte HTTPS por URL (PUT keep-alive, POST, stream SSE)
  sse_parser/                  Parser incremental de Server-Sent Events (pool estático)
  backoff/                     Backoff exponencial com teto (função pura)
cmake/strict_warnings.cmake    Avisos estritos só para o código do projeto
test/
  <componente>/                Testes unitários (Unity), correm no QEMU
  fakes/<hal>/                 Fakes dos HALs (rede, flash_fs, GPIO, ADC, LED, temperatura)
  common/                      app_main e sdkconfig partilhados pelos testes
  integration/                 Testes de integração, só na placa (Wi-Fi e Firebase reais)
  run_unit_tests.sh            Compila e corre todos os testes unitários no QEMU
Makefile                       Atalhos: build, flash, test, lint, format (make help)
```

Dependências entre componentes (sem ciclos):

```
main ─► connectivity_provider ─► backoff
main ─► device_listener ─┬─► sensor_provider ─────┬─► json_writer
main ─► sensor_provider  ├─► actuator_controller ─┴─► rgb_led, gpio_output
main ─► actuator_ctrl.   ├─► backoff
main ─► reading_uploader ─► reading_store ─► flash_fs
main ─► reading_uploader ─► data_repository ─┴─► firebase_rtdb ─┬─► firebase_auth ─┐
                                            └──────────────────┴─► http_client ─► sse_parser
```

### Memória

Nenhum request aloca memória: os clientes HTTP (PUT/PATCH, POST, stream) são criados uma vez no
`http_client_init()` e reutilizados; URLs, resposta do login e tokens JSON usam buffers estáticos;
os parsers SSE vêm de um pool estático. Só o `*_init` aloca (mutex, tasks, clientes), e cada
`*_deinit` liberta o que o respetivo `init` adquiriu.

### Concorrência

Três tasks: a principal amostra os sensores a cada 5 s e grava na flash (nunca toca na rede), a de envio
(`reading_uploader`, 1 s) lê da flash e escreve no Firebase, e a do stream do dispositivo escuta o nó. O estado
`enabled`/`command` que o stream escreve e a task principal lê é atómico. Todos os
locks têm timeout (`ESP_ERR_TIMEOUT` em vez de bloqueio infinito).

- `firebase_auth`: mutex à volta do token; cada pedido copia o token (microssegundos) e, quando é
  preciso novo login, a outra task espera por ele em vez de fazer um segundo login.
- `firebase_rtdb`: um buffer de URL para escritas (PUT/PATCH) e outro para o stream, cada um com o seu mutex; só um
  stream de cada vez (`ESP_ERR_INVALID_STATE` para um segundo).
- `http_client`: um mutex por cliente (PUT/PATCH, POST, stream), porque um handle do `esp_http_client`
  não é thread-safe. `http_client_stream_cancel()` faz o stream terminar em ~1 s.

## Hardware

- **ESP32-S3** (QFN56, 16 MB flash, 8 MB PSRAM), ligado por USB em `/dev/ttyACM0`.
- **Atuadores** (pinos no `idf.py menuconfig`):

  | Id            | Tipo        | Saída                                   | Config                    |
  |---------------|-------------|-----------------------------------------|---------------------------|
  | `light_1`     | `light`     | LED simples no GPIO6 (resistência ~330 Ω para GND) | `ACTUATOR_LIGHT_1_GPIO` |
  | `light_2`     | `light`     | LED simples no GPIO7                    | `ACTUATOR_LIGHT_2_GPIO`   |
  | `rgb_light_1` | `rgb_light` | LED RGB da placa (WS2812), na cor de `color` | `RGB_LED_GPIO` (48; 38 na DevKitC-1 v1.1) |
  | `curtain_1`   | `curtain`   | dois relés: subir no GPIO18, descer no GPIO21; `command` `up`/`down`/`stop` | `ACTUATOR_CURTAIN_UP_GPIO`, `ACTUATOR_CURTAIN_DOWN_GPIO` |
  | `relay_1`, `relay_2` | `relay` | módulo de relé; simulados (GPIO -1) | `ACTUATOR_RELAY_1_GPIO`, `ACTUATOR_RELAY_2_GPIO` |

  Um GPIO a `-1` deixa a luz simulada. Se os LEDs ligarem ao 3,3 V (em vez de GND), ative
  `ACTUATOR_LIGHT_ACTIVE_LOW`. Os outros LEDs da placa (alimentação, TX/RX) não são controláveis.

  Relés: os módulos acionam com nível **baixo** (`ACTUATOR_RELAY_ACTIVE_LOW`, ligado por omissão), e o pull-up
  interno mantém-nos desligados no arranque. Na cortina os dois relés nunca estão ligados juntos: inverter o
  sentido desliga um, espera `ACTUATOR_CURTAIN_REVERSE_DELAY_MS` (500 ms) e liga o outro. O motor fica
  ligado até chegar `stop`, por isso a cortina deve ter fins de curso.
- **Sensores**:

  | Id              | Tipo          | Origem                                                    |
  |-----------------|---------------|-----------------------------------------------------------|
  | `temperature_1` | `temperature` | sensor interno do ESP32-S3 (silício, 10–40 °C; lê acima do ambiente) |
  | `temperature_2` | `temperature` | LM35 no GPIO5 (ADC1, one-shot calibrado); temperatura do ar |
  | `luminosity_1`  | `luminosity`  | LDR no GPIO4 (ADC1, one-shot calibrado); lux aproximado   |
  | `luminosity_2`  | `luminosity`  | segundo LDR (`CONFIG_LDR_2_GPIO`); simulado por omissão (-1) |
  | `gas_1`         | `gas`         | MQ-135 no GPIO8 (ADC1, via divisor); ppm de CO2 equivalente, aproximado |

  LM35: alimentado a **5 V** (precisa de pelo menos 4 V; use o pino 5V da placa), GND comum e a saída no
  GPIO5. Dá 10 mV por °C, por isso a saída fica abaixo de 1,5 V até aos 150 °C, segura para o ADC. Sem
  LM35 ligado, `CONFIG_LM35_GPIO = -1` deixa `temperature_2` simulada (18–26 °C). A leitura do ADC tem
  ruído de uns décimos de grau.

  MQ-135 (módulo com saída analógica AO): alimentado a **5 V** (o aquecedor precisa), GND comum. A AO chega
  a 5 V, por isso **nunca** a ligue direta ao GPIO: use um divisor de 10 kΩ (AO → GPIO8) e 20 kΩ
  (GPIO8 → GND), com 100 nF do GPIO8 para GND (`MQ135_DIVIDER_*` no `menuconfig`). O valor é uma estimativa:
  `ppm = 116,6 × (Rs/R0)^-2,77`, a curva de CO2 mais usada. Para dar algo útil, o sensor precisa de **24 h de
  pré-aquecimento** e de **calibrar o R0** em ar limpo (cerca de 400 ppm): com o log de `sensor_provider` em
  debug, copie o `gas_1: Rs` para `MQ135_R0_OHMS`. A resistência de carga RL do módulo (1 kΩ ou 10 kΩ,
  marcada `102`/`103`) também está no `menuconfig`. Sem MQ-135, `CONFIG_MQ135_GPIO = -1` deixa `gas_1`
  simulado (400–1000 ppm).

  Divisor do LDR por omissão: LDR entre 3,3 V e o GPIO4, 10 kΩ do GPIO4 para GND (a tensão sobe com
  a luz). Outra ligação ou outro LDR: `menuconfig` → *Sensor Provider*.

### Mapa de pinos (ESP32-S3-DevKitC-1, módulo N16R8)

Pinos **em uso** pelo projeto:

| GPIO | Uso | Config |
|------|-----|--------|
| 4    | LDR (`luminosity_1`), ADC1 canal 3 | `CONFIG_LDR_GPIO` |
| 5    | LM35 (`temperature_2`), ADC1 canal 4 | `CONFIG_LM35_GPIO` |
| 6    | LED externo `light_1` | `ACTUATOR_LIGHT_1_GPIO` |
| 7    | LED externo `light_2` | `ACTUATOR_LIGHT_2_GPIO` |
| 8    | MQ-135 (`gas_1`), ADC1 canal 7, via divisor | `CONFIG_MQ135_GPIO` |
| 18   | relé que **sobe** a cortina (`curtain_1`), ativo a zero | `ACTUATOR_CURTAIN_UP_GPIO` |
| 21   | relé que **desce** a cortina (`curtain_1`), ativo a zero | `ACTUATOR_CURTAIN_DOWN_GPIO` |
| 48   | LED RGB da placa (WS2812), `rgb_light_1` (38 na v1.1) | `RGB_LED_GPIO` |
| 43, 44 | UART0 TX/RX, console do `idf.py monitor` | — |
| 19, 20 | USB D-/D+ (USB Serial/JTAG, console secundário) | — |
| 15–17 | UART da montagem atual (ver [ADR 0002](docs/adr/0002-atuadores.md)) | — |

Pinos **a evitar**:

| GPIO | Porquê |
|------|--------|
| 26–37 | Flash e PSRAM octal do módulo N16R8: não usar |
| 0, 3, 45, 46 | *Strapping*: definem o modo de arranque (GPIO0 é o botão BOOT) |
| 11–14 | ADC2: não pode ser lido com o Wi-Fi ativo |
| 39–42 | Pinos de JTAG: livres se não houver depurador externo |

**Livres** para novos sensores ou atuadores: GPIO1, 2, 9, 10 (ADC1, bons para sensores analógicos) e 47. O
GPIO38 também, enquanto o LED RGB estiver no 48. `relay_1` e `relay_2` estão simulados: para os ligar de
verdade, ponha um GPIO livre no `menuconfig`.

As duas ESP32-S3 têm a **mesma ligação**; só mudam o `DEVICE_ID` e o que a SPA ativa
([ADR 0005](docs/adr/0005-dispositivos-do-projeto.md)).

## Pré-requisitos

- [ESP-IDF v6.1](https://docs.espressif.com/projects/esp-idf/) instalado via EIM em `~/.espressif`
  (inclui QEMU e `esp-clang`, usados por `make test` e `make lint`).
- `clang-format` no sistema (`make format`).
- Um projeto Firebase com **Realtime Database** criado.

Os alvos do `Makefile` ativam o ESP-IDF sozinhos. Para usar `idf.py` diretamente, ative o ambiente
em cada terminal:

```sh
source ~/.espressif/tools/activate_idf_v6.1.sh
```

## Configuração do Firebase

Projeto atual: `cad-2025112840`, base de dados em `europe-west1`.

1. **Authentication → Sign-in method**: ative o fornecedor **Email/Password** e crie o utilizador
   do dispositivo (`teste@example.com`).
2. **Definições do projeto → Geral**: copie o `apiKey` da configuração da app web
   (é um identificador público, não um segredo).
3. **Realtime Database → Regras**: exija autenticação para escrever, por exemplo:

   ```json
   { "rules": { ".read": "auth != null", ".write": "auth != null" } }
   ```

   Com estas regras, a SPA também tem de se autenticar para ler/escrever. Para restringir ao
   dispositivo, compare com o UID do utilizador: `"auth.uid === '<UID>'"`.

## Configuração do dispositivo

As credenciais ficam apenas no `sdkconfig` local, nunca no código:

```sh
idf.py menuconfig
```

| Menu                   | Opção                          | Descrição                                       |
|------------------------|--------------------------------|-------------------------------------------------|
| Connectivity Provider  | `WIFI_SSID`, `WIFI_PASSWORD`   | Rede Wi-Fi (2.4 GHz)                            |
| Connectivity Provider  | `WIFI_COUNTRY_CODE`            | País dos canais Wi-Fi (`PT` ativa os canais 12-13) |
| Firebase HTTP Client   | `FIREBASE_DB_URL`              | URL do Realtime Database, sem `/` no final      |
| Firebase Auth          | `FIREBASE_API_KEY`             | `apiKey` da app web do Firebase                 |
| Firebase Auth          | `FIREBASE_USER_EMAIL`          | Email do utilizador (vazio = sem autenticação)  |
| Firebase Auth          | `FIREBASE_USER_PASSWORD`       | Senha do utilizador                             |
| Firebase Paths         | `DEVICE_ID`                    | Identificador do dispositivo (`device-01`)      |

O `sdkconfig.defaults` (versionado) fixa o target (`esp32s3`), a flash de 16 MB, a partição de
app de 1,5 MB, a stack da task principal (8 KB, necessária para o handshake TLS), o URL da base de
dados e o email do dispositivo.

A **senha** e a API key ficam apenas no `sdkconfig` local, que está no `.gitignore`. Atenção:

- a senha fica em texto simples no `sdkconfig` e na imagem gravada na flash;
- não inclua o `sdkconfig` no `.zip` de entrega (o `sdkconfig.defaults` basta para recriá-lo);
- use um utilizador dedicado ao dispositivo, com acesso apenas aos dados de que precisa.

## Compilar, gravar e monitorizar

```sh
make build
make flash-monitor              # PORT=/dev/ttyACM0 por omissão; Ctrl+] sai do monitor
make help                       # lista todos os alvos
```

| Alvo                          | O que faz                                                          |
|-------------------------------|--------------------------------------------------------------------|
| `make build`                  | Compila o firmware                                                 |
| `make flash` / `monitor` / `flash-monitor` | Grava / abre o monitor em `PORT`                      |
| `make test`                   | Testes unitários no QEMU                                           |
| `make test-integration`       | Grava os testes de integração na placa e abre o monitor            |
| `make lint`                   | `idf.py clang-check` com o `.clang-tidy`; falha com achados no projeto |
| `make format` / `format-check`| Aplica / verifica o `clang-format`                                 |
| `make check`                  | `format-check` + `lint` + `build` + `test` (tudo o que não precisa da placa) |
| `make clean`                  | Apaga os builds (firmware, lint e testes)                          |

Se a gravação não conseguir ligar-se à placa (`Failed to connect`), entre em modo de download à
mão: mantenha **BOOT**, carregue em **RESET**, solte **BOOT** e repita o comando.

Saída esperada no monitor:

```
I (...) connectivity: Got IP 192.168.1.42
I (...) connectivity: Online and time synced
I (...) firebase_auth: Signed in as teste@example.com
I (...) main: Catalog published
I (...) main: Publishing sensors {}, actuators {}
```

Numa base de dados nova nada está ativo. Na consola do Firebase, ponha
`devices/<DEVICE_ID>/sensors/temperature_1/enabled = true` e
`devices/<DEVICE_ID>/actuators/light_1/{enabled,command} = true`; o LED do GPIO6 acende, o ESP32
regista `light_1 -> ON` e passa a publicar `sensors {"temperature_1":22.4}, actuators {"light_1":true}`.
O mesmo vale para `light_2` (GPIO7) e `rgb_light_1` (LED RGB); neste, `color = "#ff8000"` (texto)
muda a cor, e a leitura passa a trazer `"rgb_light_1": {"on": true, "color": "#ff8000"}`. Na cortina,
`command` é texto: `"up"`, `"down"` ou `"stop"`, e a leitura traz `"curtain_1": "up"`.
As regras do Realtime Database têm de deixar o utilizador do ESP escrever em `devices/` e `readings/`.

## Testes

### Unitários (QEMU)

Cada componente com lógica própria tem um projeto Unity em `test/<componente>/`. Só o HAL de rede
(`http_client`) é substituído por um fake (`test/fakes/http_client`); as camadas acima entram reais,
por isso os testes cobrem também a integração entre os componentes do projeto. Correm no QEMU
(esp32s3), sem placa:

```sh
make test                                 # todos
test/run_unit_tests.sh firebase_auth      # só um
```

| Projeto               | O que cobre                                                                 |
|-----------------------|-----------------------------------------------------------------------------|
| `backoff`             | primeiro atraso, duplicação, teto, sem overflow                             |
| `sse_parser`          | chunks partidos, CRLF, vários eventos, linha longa, reset, pool esgotado    |
| `json_writer`         | objeto vazio, vírgulas, cabe à justa, overflow e truncagem                  |
| `sensor_provider`     | chip e LDR (ativo/inativo/erro), conversão mV → lux, só sensores ativos, catálogo |
| `chip_temperature`    | HAL real no QEMU: init/deinit/reinit                                         |
| `actuator_controller` | saída só com enabled + command, cor (#rrggbb, inválida, default), GPIO/simulado, erro do driver, JSON |
| `gpio_output`         | HAL real no QEMU: GPIO válido/inválido, pull-up, níveis, deinit/reinit      |
| `http_client`         | init/deinit/reinit e validação de URL, PUT/PATCH/POST/stream (sem rede)     |
| `firebase_auth`       | login, cache, renovação, erros/retry, `expiresIn` inválido, 2 tasks → 1 login |
| `firebase_rtdb`       | URL com token, PUT e PATCH, token máximo, path longo, falha do login, 1 stream |
| `data_repository`     | URL `readings/<ID>/<timestamp>.json`, formato da leitura, PATCH do catálogo |
| `device_listener`     | snapshot, put/patch (chaves com `/`, `null`, não booleano), backoff, deinit |

Os nomes seguem `test_<modulo>_<cenario>_<resultado_esperado>`.

### Integração (só na placa)

`test/integration/` usa Wi-Fi e Firebase reais: liga-se, sincroniza a hora, faz login, grava uma
leitura em `readings/integration-test` e o catálogo em `devices/integration-test`, ativa um sensor
e confirma que o stream aplica o snapshot e uma alteração ao vivo, cancela o stream, e
desliga e volta a ligar toda a pilha (`deinit`/`init`). O último teste apaga os nós
`devices/integration-test` e `readings/integration-test`, para não deixar dados de teste no Firebase. Reutiliza o `sdkconfig` do produto
(credenciais incluídas), por isso configure primeiro o projeto principal. O `sdkconfig` da
integração é derivado: sempre que o `sdkconfig` do produto (ou um `sdkconfig.defaults`) muda, o
próximo build recria-o automaticamente — basta usar o `idf.py menuconfig` na raiz. Os overrides de
`test/integration/sdkconfig.defaults` (ex.: `DEVICE_ID=integration-test`) mantêm-se.

```sh
make test-integration          # grava e abre o monitor (Ctrl+] para sair)
```

### Análise estática

`make lint` corre o [IDF Clang-Tidy](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-guides/tools/idf-clang-tidy.html)
(`idf.py clang-check`, toolchain clang num build separado `build_clang/`) com o `.clang-tidy` do
projeto. O relatório completo fica em `warnings.txt`. O MISRA com `cppcheck --addon=misra` fica para
quando o cppcheck estiver instalado.

# ADR 0004 — Persistência local: leituras sempre na flash e configuração `enabled`

- **Estado:** aceite (implementação em curso)
- **Data:** 2026-11-16 (revê o rascunho de 2026-11-13)
- **Âmbito:** dispositivo IoT; o formato dos dados segue a [ADR 0001](0001-modelo-de-dados.md), os sensores a
  [ADR 0003](0003-sensores.md), os atuadores a [ADR 0002](0002-atuadores.md) e as placas a
  [ADR 0005](0005-dispositivos-do-projeto.md) (duas ESP32-S3 de 16 MB de flash)

## Contexto

Hoje o dispositivo só funciona com rede:

- **Uma leitura que não chega ao Firebase perde-se.** `publish_reading()` regista o erro e segue.
- **Offline, o laço deixa de cumprir os 5 s.** O `PUT` bloqueia até 10 s (`HTTP_CLIENT_REQUEST_TIMEOUT_MS`), com
  uma repetição, e amostragem e envio partilham a mesma task.
- **Arrancar sem Wi-Fi ou sem SNTP reinicia o dispositivo em ciclo** (`ESP_ERROR_CHECK` em `app_main`).
- **O `enabled` de cada sensor e atuador vive só em RAM** e começa a `false` depois de cada reset.
- **O timestamp é tirado no envio** (`data_repository_push_reading`), não na amostragem.
- **Não há relógio de tempo real.** O relógio só existe depois da primeira sincronização SNTP e perde-se num
  reset. Uma leitura sem relógio válido seria gravada na chave errada.

## Decisão

### 1. Duas tasks e a flash no meio

```
task de amostragem (5 s)  →  flash (LittleFS)  →  task de envio (1 s)  →  Firebase
```

- **Amostragem:** a cada 5 s, se o relógio é válido e há pelo menos um sensor ativo, tira o timestamp, monta o
  registo e **acrescenta-o sempre à flash**. Nunca toca na rede, por isso nunca bloqueia.
- **Envio:** a cada 1 s, se há registos por enviar, envia **até 5 registos** (os mais antigos) num único
  `PATCH` multi-caminho em `readings/<DEVICE_ID>` (uma chave `<timestamp_ms>` por registo). Com 2xx, avança.
- **A ordem de chegada não importa.** O RTDB ordena por chave. A SPA usa `limitToLast(200)` e o motor de regras
  `limitToLast(1)`: um registo antigo que chega depois de um recente fica fora da janela e não dispara nada.
- **Ritmo:** o envio drena 5 registos/s e a amostragem gera 0,2/s, por isso um atraso de 4 dias (cerca de
  60 mil registos) escoa-se em cerca de 4 h. Online, o atraso é de 1 s.
- **Falha de envio, de qualquer tipo (rede, 401, 5xx ou 4xx):** nada se apaga e o lote repete-se com o
  `backoff` existente (teto de 30 s). Mantém-se assim **por ora**, para não perder dados. O risco conhecido é
  um registo que o Firebase rejeite para sempre bloquear a fila; cada falha fica no log com o código HTTP, e a
  política revê-se se isso acontecer.
- **O `PUT`/`PATCH` com a mesma chave é idempotente:** reenviar é seguro. Um reset a meio de um envio nunca
  duplica dados.

### 2. Relógio válido

**Relógio válido** significa "o SNTP sincronizou desde este arranque". Enquanto isso não acontecer, não se
amostra. Se o Wi-Fi cair depois, o relógio continua a contar e os registos guardados mantêm timestamps
corretos. O desvio é de segundos por dia, por isso **não se trata o salto do relógio no re-sync**.

O reset desfaz o relógio e não se reconstrói a partir de um timestamp guardado, porque não se sabe quanto tempo
o dispositivo esteve desligado. Os registos já guardados seguem quando houver ligação; a amostragem só recomeça
com relógio.

### 3. Arranque sem rede

O dispositivo deixa de abortar quando o Wi-Fi ou o SNTP falham. O `connectivity_provider_init()` arranca o
Wi-Fi e o SNTP **em segundo plano** (com o `backoff` que já existe) e devolve logo. O estado da rede vem do
`esp_event` (`IP_EVENT_STA_GOT_IP` liga e `IP_EVENT_STA_LOST_IP` ou `WIFI_EVENT_STA_DISCONNECTED` desligam o
bit `CONNECTED_BIT` do event group). O SNTP não tem evento: o `sync_cb` da configuração liga o `TIME_VALID_BIT`.
Expõe `connectivity_provider_is_connected()` e `connectivity_provider_is_time_valid()`. A task de envio só
tenta a rede com a ligação ativa, por isso offline não gasta timeouts.

### 4. Estado de cada fase

| Estado | Amostragem | Envio |
|--------|------------|-------|
| Online, relógio válido | sim | sim, 1 s |
| Perdeu a ligação, relógio válido | sim | em pausa até `CONNECTED_BIT`; os registos ficam na flash |
| Sem relógio (arranque sem rede, SNTP por concluir) | **não** | só o que já estava guardado |
| Nenhum sensor ativo | **não** | — |

### 5. Registo e armazenamento

- **Registo binário de tamanho fixo (56 B):** timestamp em ms (8 B), `float` de cada sensor (`NaN` quando o
  sensor está desativado ou sem leitura válida), um `uint32` por atuador e CRC32 (4 B). O `uint32` do atuador
  junta o `enabled` (bit 31), o estado (ligado/desligado ou parada/sobe/desce) e a cor `0xRRGGBB`, de modo
  que a cortina (três estados) e o RGB cabem sem tratamento especial. Um `static_assert` liga o tamanho a
  `SENSOR_COUNT` e `ACTUATOR_COUNT`.
- **Segmentos:** ficheiros `seg_<n>.bin` na LittleFS, de **256 registos** (cerca de 14 KB, 21 min), com um
  cabeçalho (magia e versão do formato). Um segmento de versão diferente descarta-se. Os registos só se
  acrescentam e cada um é sincronizado (`fsync`) ao ser escrito: um corte de energia perde no máximo o registo
  em curso, e o CRC identifica um registo final truncado.
- **Desgaste:** a cerca de 17 mil registos por dia, e com o nivelamento da LittleFS sobre 4 MB (1024 blocos
  de 4 KB), cada bloco é apagado poucas dezenas de vezes por dia, muito abaixo das 100 mil escritas do bloco.
- **Envio e apagar:** o envio guarda em RAM a posição já confirmada. Um segmento só se apaga depois de todos os
  seus registos terem 2xx e de a amostragem já ter passado ao seguinte. Depois de um reset, o primeiro
  segmento recomeça do início (no máximo 256 registos repetidos, o que a chave idempotente permite).
- **Cheio:** quando o espaço livre cai abaixo de **3 segmentos**, apaga-se o segmento mais antigo, mesmo que
  não enviado. As leituras recentes são as que a SPA mais quer. A reserva dá folga à LittleFS para compactar.
- **RAM:** buffers estáticos; sem `malloc` fora do `init`. O envio usa um buffer estático para 5 registos.

### 6. Configuração `enabled` (LittleFS)

- **O que se guarda:** o `enabled` de cada sensor e de cada atuador. O `command` **não** se guarda: depois de
  um reset os atuadores arrancam desligados, e o primeiro `put` do stream repõe o comando.
- **Onde:** um único ficheiro `config.bin` na mesma LittleFS (versão, máscaras, CRC32). Escreve-se num
  ficheiro temporário e faz-se `rename`, para o corte de energia nunca deixar uma cópia a meio.
- **Quando:** só quando um valor muda.
- **Quem manda:** continua a ser a SPA. O primeiro `put` do stream depois de ligar sobrepõe a cópia local.
  A cópia local vale entre o arranque e esse primeiro evento, ou enquanto não há rede.
- **A config é lida antes de cada amostragem.** O `sensor_provider_read()` já só lê sensores ativos.
- O NVS fica só com o Wi-Fi e o PHY, e continua a ser iniciado pelo `connectivity_provider`.

### 7. Partição

Não há OTA (trabalho académico), por isso um só slot de aplicação. A flash das duas placas é de **16 MB** (os
8 MB são a PSRAM). A tabela de partições é própria do projeto (`partitions.csv`):

| Partição | Tamanho | Uso |
|----------|---------|-----|
| `nvs` | 24 KB | Wi-Fi |
| `phy_init` | 4 KB | rádio |
| `factory` | 1,5 MB | aplicação (a atual) |
| `storage` | **4 MB** | LittleFS: leituras e `config.bin` |

Autonomia offline: a 56 B por registo e 5 s por leitura são cerca de 1 MB/dia. Os 4 MB (menos a reserva)
dão **cerca de 3 dias** sem rede. O alvo é só a ESP32-S3; os alvos de 4 MB ficam de fora (YAGNI).

### 8. Estrutura

Segue as camadas da arquitetura do projeto:

- **HAL `flash_fs`:** wrapper fino sobre o componente gerido `joltwallet/littlefs` (monta, abre, lê, escreve,
  `fsync`, `rename`, apaga, lista, espaço livre). Nos testes é substituído por um fake em RAM.
- **`reading_store`** (negócio): segmentos, CRC, despejo do mais antigo, posição do envio.
- **`config_store`** (negócio): `config.bin` com o `enabled` de sensores e atuadores.
- **`reading_uploader`** (negócio): a task de envio de 1 s.
- `main`: a task de amostragem; `data_repository` ganha o envio em lote.

## Alternativas consideradas

| Alternativa | Porque não |
|-------------|------------|
| Enviar direto e guardar só o que falha | Cada leitura online faria uma escrita de rede na task de amostragem, e offline o laço deixaria de cumprir os 5 s. Gravar sempre desacopla as duas tasks. |
| Guardar `enabled` no NVS | Obriga a um segundo HAL (`kv_store`) e à troca do dono do `nvs_flash_init`, por poucos booleanos. O `config.bin` com `rename` dá a mesma atomicidade. |
| Anel direto em `esp_partition` | Dispensaria a dependência, mas o sistema de ficheiros cobre também a configuração e o nivelamento do desgaste. |
| Guardar cada leitura em JSON | Cerca de 200 B ou mais por registo, 4× menos autonomia. |
| Parar de gravar quando enche | Preserva o início da queda, mas ao voltar a SPA fica sem os dados mais recentes. |
| Decimar quando enche | Mais complexo. Pode entrar depois se a autonomia não chegar. |
| Gerar timestamp sem relógio | A SPA ordena por timestamp: chaves erradas misturar-se-iam com dados válidos. |
| Persistir o último timestamp e continuar depois de um reset | Não se sabe quanto tempo esteve desligado. |
| Guardar também o `command` dos atuadores | Um atuador a ligar sozinho depois de um reset, sem a SPA confirmar, é inseguro. |

## Consequências

- Uma queda de rede não perde dados enquanto o relógio estiver válido e a partição não encher (cerca de 3 dias).
- A amostragem mantém os 5 s mesmo sem rede, e o dispositivo deixa de entrar em ciclo de reset sem rede.
- O `enabled` sobrevive a resets. Um sensor desativado pela SPA continua desativado mesmo offline.
- **Escritas na flash também online:** uma por leitura. O desgaste fica folgado (ponto 5).
- **Novos componentes e partição:** `joltwallet/littlefs`, `partitions.csv`, os HALs `flash_fs` e os módulos
  `reading_store`, `config_store` e `reading_uploader`. O `README` e o `CLAUDE.md` passam a mencionar a partição.
- **O formato do registo depende de `SENSOR_COUNT` e `ACTUATOR_COUNT`.** Um sensor novo muda a versão do
  formato, e os segmentos antigos perdem-se (aceitável aqui).
- **Testes:** os unitários correm no QEMU com o fake do HAL (despejo, CRC de registo truncado, versão diferente,
  posição do envio, reinício). Os de integração (Wi-Fi a cair, reset offline) só correm na placa.
- **Risco aceite:** um registo que o Firebase rejeite para sempre bloqueia a fila (ver ponto 1).

# ADR 0001 — Modelo de dados no Firebase Realtime Database

- **Estado:** aceite
- **Data:** 2026-11-08
- **Âmbito:** dispositivo IoT (este repositório) e SPA

## Contexto

O enunciado obriga a que a SPA e o dispositivo IoT comuniquem **apenas** através do Firebase Realtime
Database. O modelo de dados tem de suportar:

1. **Dados em tempo real** no ecrã principal (requisito g).
2. **Divisões** com sensores e atuadores (requisito b), e um 3.º ecrã que **adiciona e remove** sensores
   e atuadores em cada divisão; na Época Normal, também cria **novas divisões** dinamicamente.
3. **Regras de automação** definidas na SPA (requisito e).
4. Um dispositivo com recursos limitados (ESP32-S3): uma só ligação de stream (SSE), sem descarregar
   dados de que não precisa, e escritas pequenas.

O primeiro modelo (`iot-messages/<device>/<timestamp>/data`, com o nó `actuator` ao lado dos
timestamps) misturava o histórico e os comandos no mesmo nó. Isso impedia a SPA de ler só a última
leitura e, se o dispositivo escutasse o nó do dispositivo, obrigava-o a descarregar todo o histórico.

## Decisão

Três nós de topo, cada um com um papel e com donos bem definidos por campo:

```
devices/<deviceId>/                        descrição e configuração; o ESP escuta este nó (1 stream)
  sensors/<sensorId>:
    type: "temperature"                    ESP   tipo do sensor
    unit: "°C"                             ESP   unidade (omitida em sensores sem unidade)
    simulated: true                        ESP   true enquanto não houver driver real
    enabled: true                          SPA   o sensor está em uso
  actuators/<actuatorId>:
    type: "light"                          ESP   "light", "relay", "rgb_light" ou "curtain"
    simulated: true                        ESP
    enabled: true                          SPA   o atuador está em uso
    command: true                          SPA   estado pedido (ligado/desligado); no tipo "curtain",
                                                 o texto "up", "down" ou "stop"
    color: "#ff8000"                       SPA   só tipo "rgb_light": cor pedida (em falta: branco #f0f0f0)

readings/<deviceId>/<timestamp_ms>:        histórico; o ESP escreve a cada 5 s
  sensors:   { <sensorId>: valor }         só sensores ativos
  actuators: { <actuatorId>: true|false }  estado real dos atuadores ativos; um "rgb_light"
                                           reporta { on: true|false, color: "#rrggbb" } e uma
                                           "curtain" o texto "up", "down" ou "stop"

home/                                      só a SPA lê e escreve
  rooms/<roomId>:
    name: "Sala"
    sensors:   { <deviceId>: { <sensorId>: true } }
    actuators: { <deviceId>: { <actuatorId>: true } }
  labels/<deviceId>/<kind>/<itemId>: "Temperatura do chip"   nome a mostrar (opcional)
  settings/location: {name, latitude, longitude}             para as APIs externas
  rules/<ruleId>                           regras de automação (SPA escreve)
  rule_status/<ruleId>                     estado de cada regra (motor de regras escreve)
```

Exemplo de leitura: `readings/device-01/1762630000000 = {"sensors": {"temperature_1": 22.4,
"luminosity_1": 320}, "actuators": {"light_1": true, "rgb_light_1": {"on": true, "color": "#ff8000"},
"curtain_1": "up"}}`.

Tipos de atuador: `light` e `relay` (liga/desliga), `rgb_light` (liga/desliga e cor `"#rrggbb"`) e
`curtain` (`command` em texto: `"up"`, `"down"` ou `"stop"`). Os atuadores, o hardware e os drivers
estão na [ADR 0002](0002-atuadores.md), e a distribuição pelos dois dispositivos na
[ADR 0005](0005-dispositivos-do-projeto.md).

### Regras de cada nó

- **Catálogo (`devices/<id>`)**: o firmware conhece os sensores e atuadores que tem (reais ou
  simulados) e, no arranque, publica `type`, `unit` e `simulated` com um **PATCH multi-path**
  (`{"temperature_1/type": "temperature", ...}`). O PATCH só altera esses campos, por isso não apaga
  `enabled` nem `command`, que pertencem à SPA. A SPA nunca escreve campos do ESP, e vice-versa.
- **Adicionar e remover**: adicionar um sensor a uma divisão é pôr `enabled: true` e referenciá-lo em
  `home/rooms/<roomId>`; remover é o inverso. Um `enabled` ou `command` em falta vale `false`: numa
  base de dados nova nada está ativo até a SPA o ativar.
- **No ESP**: um sensor desativado não é lido nem publicado; um atuador desativado ignora o `command` e
  fica desligado. O stream entrega cada alteração (put/patch) e o ESP aplica-a sem reiniciar.
- **Pedido e confirmação**: a SPA pede em `command`; o estado real chega nas `readings`. Assim a SPA
  distingue "pedido" de "aplicado" e percebe quando o dispositivo está offline.
- **Leituras**: a chave é o epoch em milissegundos (13 dígitos), por isso a ordem lexicográfica das
  chaves é a ordem cronológica. A SPA usa `query(ref(db, "readings/<id>"), limitToLast(200))` com
  `onValue`: uma janela deslizante das últimas 200 leituras, que alimenta os gráficos de histórico, e
  cuja leitura mais recente alimenta os cartões. A cada leitura nova, o SDK descarrega só essa
  leitura, e não a janela inteira nem o histórico. O ESP só publica depois de sincronizar o relógio
  (SNTP).
- **Nomes (labels)**: ficam em `home/labels`, e não no catálogo `devices/`, porque o ESP escuta
  `devices/<id>` inteiro e receberia cada alteração de nome sem lhe servir de nada. Pertencem ao
  item, não à divisão, por isso mantêm-se se o item mudar de divisão. Sem label, a SPA mostra o id.
- **Divisões**: existem só na SPA. Uma divisão referencia sensores e atuadores por
  `<deviceId>/<sensorId>`, por isso suporta vários dispositivos IoT e mudar a casa não exige mudar o
  firmware.
- **Regras de automação**: definidas na SPA e avaliadas por um motor Node.js, que lê `readings/` de
  qualquer dispositivo e escreve `command` em qualquer outro. Uma regra que usa dados do dispositivo
  A para agir no dispositivo B fica num só lugar. Formato e disparo:
  [ADR 0001 da SPA](../../../spa/docs/adr/0001-motor-de-regras.md).

## Alternativas consideradas

| Alternativa | Porque não |
|-------------|------------|
| Nó `latest` com a última leitura, ao lado do histórico | Duplica escritas; `limitToLast(1)` dá o mesmo resultado à SPA. |
| Manter leituras e atuadores no mesmo nó do dispositivo (modelo inicial) | Na ordenação por chave, `actuator` vem depois dos timestamps, então `limitToLast(1)` devolve-o em vez da última leitura; o stream do ESP descarregaria o histórico. |
| Divisões definidas no firmware | Contraria o 3.º ecrã e a adenda (criar divisões na SPA); qualquer mudança na casa exigiria gravar o firmware. |
| `enabled` num nó de controlo separado do catálogo | Duplica os ids em dois sítios; com donos por campo, o catálogo e o estado vivem juntos. |
| Publicar o catálogo com PUT | O PUT substitui o nó e apagaria `enabled` e `command` escritos pela SPA. |
| Regras no ESP | Uma regra que cruza dispositivos precisaria de dados de outros dispositivos na borda; adiado. |

## Consequências

**Positivas**

- O ESP mantém uma só ligação SSE, sobre um nó pequeno (`devices/<id>`), e escreve uma leitura por ciclo.
- A SPA obtém dados em tempo real sem descarregar o histórico.
- Gerir divisões, sensores e atuadores não exige alterar o firmware.
- Cada campo tem um único dono, o que evita escritas concorrentes sobre o mesmo valor.

**Negativas e limites**

- O histórico cresce sem limite: cerca de 150 B × 17 280 leituras/dia ≈ 2,6 MB/dia (o plano gratuito
  tem 1 GB). Não há limpeza automática por agora.
- A SPA só pode ativar sensores e atuadores que o firmware conheça; os simulados disponíveis estão numa
  tabela fixa no firmware.
- As regras dependem do motor Node.js estar a correr.
- O estado real de um atuador chega à SPA na leitura seguinte (até 5 s depois do comando).
- Uma leitura sem nada ativo (`{"sensors": {}, "actuators": {}}`) não fica guardada: o Firebase não
  guarda objetos vazios.

## Trabalho futuro

- Regras de segurança do Realtime Database (ficheiro de configuração entregue): o utilizador do ESP só
  escreve `readings/<id>` e os campos `type`/`unit`/`simulated` do seu catálogo.
- Limpeza do histórico antigo, se o volume vier a importar.
- Reavaliar regras na borda para automações locais e críticas (que devem funcionar sem a SPA).

# ADR 0005 — Duas ESP32-S3 com o mesmo firmware: catálogo único e divisões na SPA

- **Estado:** aceite
- **Data:** 2026-11-13
- **Âmbito:** dispositivo IoT; o formato dos dados segue a [ADR 0001](0001-modelo-de-dados.md), os atuadores a
  [ADR 0002](0002-atuadores.md) e os sensores a [ADR 0003](0003-sensores.md)

## Contexto

A casa do projeto tem três divisões e usa **duas ESP32-S3**:

| Divisão | Itens |
|---------|-------|
| Sala de estar | 1 LED (iluminação), 1 LED RGB (ambiente), 1 LDR, 1 cortina (um relé para subir e outro para descer), sensor de temperatura do chip |
| Quarto | 1 LED, 1 LDR, 1 relé simulado (AC), 1 LM35 |
| Cozinha | 1 sensor de gás, 1 LED, 1 LDR simulado, 1 relé simulado |

A primeira ESP fica com a sala e a segunda com o quarto e a cozinha. O que não existe fisicamente é
simulado. Queremos **a mesma ligação nas duas placas** e o **mesmo firmware**, e decidir na aplicação o que
está em uso em cada uma.

Pela [ADR 0001](0001-modelo-de-dados.md), as divisões existem só na SPA (`home/rooms`) e o firmware só
conhece os itens que tem. Isso já favorece um firmware único.

## Decisão

### Um firmware, um catálogo

Cada ESP publica o **mesmo catálogo**, a união do que as duas precisam. A ligação física é igual nas duas, e
só muda o `CONFIG_DEVICE_ID` (e as credenciais). Um item sem nada ligado fica com `enabled` a `false`. A SPA
é quem liga o que existe em cada placa e o distribui pelas divisões.

| Item | Id | Origem | Pino |
|------|----|--------|------|
| Temperatura do chip | `temperature_1` | sensor interno | — |
| Temperatura do ar | `temperature_2` | LM35 (ADC1), 5 V | GPIO5 |
| Luz 1 | `luminosity_1` | LDR (ADC1) | GPIO4 |
| Luz 2 | `luminosity_2` | LDR simulado (`CONFIG_LDR_2_GPIO = -1`) | — |
| Gás | `gas_1` | MQ-135 (ADC1), via divisor, 5 V | GPIO8 |
| LED 1 | `light_1` | LED externo | GPIO6 |
| LED 2 | `light_2` | LED externo | GPIO7 |
| LED RGB | `rgb_light_1` | WS2812 da placa | GPIO48 |
| Cortina | `curtain_1` | dois relés, subir e descer | GPIO18 e GPIO21 |
| Relé 1 | `relay_1` | simulado (`CONFIG_ACTUATOR_RELAY_1_GPIO = -1`) | — |
| Relé 2 | `relay_2` | simulado (`CONFIG_ACTUATOR_RELAY_2_GPIO = -1`) | — |

### Que itens usa cada divisão

| Divisão (ESP) | Sensores | Atuadores |
|---------------|----------|-----------|
| Sala (ESP 1) | `temperature_1`, `luminosity_1` | `light_1`, `rgb_light_1`, `curtain_1` |
| Quarto (ESP 2) | `temperature_2`, `luminosity_1` | `light_1`, `relay_1` (simulado) |
| Cozinha (ESP 2) | `gas_1`, `luminosity_2` (simulado) | `light_2`, `relay_2` (simulado) |

Na ESP 2, `light_1` é o LED do quarto e `light_2` o da cozinha, e `luminosity_1` o LDR do quarto. É a SPA que
liga cada item à sua divisão em `home/rooms/<roomId>`, com os nomes (por exemplo `curtain_1` como "Cortina")
em `home/labels`. O firmware não sabe o que é uma divisão.

### Cortina: um atuador `curtain` com comando de texto

`curtain_1` é um só atuador (tipo `curtain`) com dois relés. O seu `command` é o texto `"up"`, `"down"` ou
`"stop"`, e o estado real das leituras usa as mesmas palavras. Isto muda o modelo da ADR 0001, porque até
aqui o `command` era sempre um booleano.

- **Interlock:** os dois relés nunca estão ligados ao mesmo tempo. Inverter o sentido desliga um, espera
  `CONFIG_ACTUATOR_CURTAIN_REVERSE_DELAY_MS` (500 ms) e liga o outro. Os pormenores estão na ADR 0002.
- **Seguro por omissão:** um comando em falta, inválido, ou um atuador desativado, pára a cortina. Depois de
  um reset a cortina arranca parada.
- **Relés ativos a zero** (módulos de relé que acionam com nível baixo): o pino arranca já desligado, com o
  pull-up interno, para não haver um pulso ao ligar.

### O que sai do catálogo

`humidity_1` e `motion_1` (simulados) saem, porque nenhum dispositivo do projeto os usa. Com eles sai o
suporte a sensores booleanos. Entra `luminosity_2` para o LDR simulado da cozinha, e `relay_1` e `relay_2`
(tipo `relay`, ligar/desligar) para os relés simulados.

## Alternativas consideradas

| Alternativa | Porque não |
|-------------|------------|
| Um firmware diferente por ESP (catálogo, pinos e `#ifdef` por divisão) | Duplica configuração e testes, e uma mudança na casa obriga a recompilar e a gravar. Com a ligação igual, `enabled` já separa o que cada placa tem. |
| Dois atuadores `relay` para a cortina (`curtain_up` e `curtain_down`), com interlock | Mantém o `command` booleano, mas a SPA teria de coordenar dois atuadores e o "parar" seria desligar os dois. Um só atuador com `up`/`down`/`stop` é mais fiel ao que é uma cortina. |
| Manter `humidity_1` e `motion_1` simulados | Ficam no catálogo e no JSON sem uso. |
| Pinos diferentes em cada ESP | Obriga a configurações diferentes e a ligar as placas de maneira diferente. |

## Consequências

- **A SPA mudou** (repositório `../spa`): conhece os tipos `relay`, `curtain` e `gas`; a cortina tem botões
  Subir, Parar e Descer (o `command` é texto) e as regras de automação aceitam `up`, `down` e `stop` como
  ação; o cartão "Presença" deu lugar a "Gás" e o interior mostra a luminosidade em vez da humidade. Os nós
  antigos de `humidity_1` e `motion_1` em `devices/<id>/sensors` ficam órfãos na base de dados e têm de ser
  apagados à mão.
- **Um item desativado fisicamente mas com `enabled: true` na SPA** publica lixo (um pino do ADC ao ar) ou
  comanda um pino sem nada ligado. A disciplina é da SPA: só ativa o que a placa tem.
- **A cortina não tem fim de curso nem tempo limite no firmware.** O motor fica ligado até chegar `"stop"`.
  Parte-se do princípio de que a cortina tem fins de curso. Se não tiver, é preciso um tempo máximo de
  funcionamento (questão em aberto).
- **A pausa de inversão (500 ms) bloqueia** a task do `device_listener`, o que atrasa outros comandos desse
  momento em meio segundo.
- **Os relés simulados** (`relay_1`, `relay_2`) passam a reais com um GPIO no `menuconfig`, sem mudar código.
- **O limite de tokens JSON do listener** subiu de 128 para 192, porque o nó completo do dispositivo (6
  atuadores e 5 sensores) deixou de caber nos 128. Um teste monta esse nó a partir dos catálogos reais.

## Questões em aberto

1. **Tempo máximo da cortina:** é preciso parar sozinha ao fim de N segundos se não houver fim de curso?
2. **Relé AC do quarto:** se um dia ligar uma carga de rede, convém um relé com isolamento e um fusível;
   esta ADR só trata da parte do firmware.

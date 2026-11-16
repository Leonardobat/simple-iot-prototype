# ADR 0002 — Atuadores: tipos, hardware e drivers

- **Estado:** aceite
- **Data:** 2026-11-09
- **Âmbito:** dispositivo IoT (este repositório); o formato dos dados segue a
  [ADR 0001](0001-modelo-de-dados.md)

## Contexto

O enunciado exige pelo menos 2 tipos de atuadores, e pelo menos um real. A placa de desenvolvimento
(ESP32-S3-DevKitC-1, módulo WROOM-1-N16R8) tem:

- um **LED RGB endereçável** (WS2812), o único LED da placa ligado a um GPIO: GPIO48 na v1.0 e na
  maioria das placas compatíveis, GPIO38 na v1.1;
- três LEDs que não são controláveis: alimentação e TX/RX do conversor USB-UART.

Para mais saídas, ligam-se LEDs externos a GPIOs livres. Os GPIO15–17 estão ocupados pela UART nesta
montagem; o GPIO6 e o GPIO7 estão livres e não são pinos de arranque (*strapping*), de flash/PSRAM nem
de USB.

O firmware segue camadas (negócio → drivers → HAL) e testa no QEMU, onde só o HAL é substituído por
um fake.

## Decisão

### Atuadores e tipos

| Id            | Tipo        | Saída                                     | Campos da SPA                  |
|---------------|-------------|-------------------------------------------|--------------------------------|
| `light_1`     | `light`     | LED externo no GPIO6                      | `enabled`, `command`           |
| `light_2`     | `light`     | LED externo no GPIO7                      | `enabled`, `command`           |
| `rgb_light_1` | `rgb_light` | LED RGB da placa (WS2812)                 | `enabled`, `command`, `color`  |

- **`light`**: liga e desliga.
- **`rgb_light`**: uma luz mais específica, que também tem cor. A cor é texto `"#rrggbb"` (como no
  `<input type="color">` da SPA, sem conversão). Se faltar ou for inválida, acende branco
  (`#f0f0f0`, ~94 % do brilho, bem visível) e o ESP regista um aviso. O brilho vem da própria cor:
  `#202020`, por exemplo, é branco a 1/8.
- Os pinos estão no `menuconfig`. Um GPIO a `-1` deixa a luz sem saída e `simulated: true` no catálogo,
  o que permite testar sem LEDs ligados.

### Estado

- Uma saída só está ligada com `enabled` **e** `command` a `true`.
- O estado publicado em `readings/` é o que o driver **aplicou de facto**, e não o pedido. Se o driver
  falhar, a saída fica como estava, a leitura mostra-o e o comando seguinte tenta de novo. Um
  `rgb_light` reporta `{on, color}` com a última cor mostrada.
- Uma saída só é acionada quando o estado muda: repetir o mesmo comando ou a mesma cor não gera uma
  nova escrita no hardware.

### Camadas

```
device_listener (stream)  →  actuator_controller (negócio: estado, cor, JSON)
                                 ├─► rgb_led      (HAL)  →  espressif/led_strip  →  RMT
                                 └─► gpio_output  (HAL)  →  driver/gpio
```

- O `actuator_controller` tem uma tabela fixa: id, tipo, se é simulado, se tem cor e o tipo de saída
  (`OUTPUT_NONE`, `OUTPUT_GPIO` ou `OUTPUT_RGB_LED`). Juntar um atuador é juntar uma linha.
- Os HALs são finos e não sabem nada de Firebase nem de atuadores.

### LED RGB: porquê RMT

O WS2812 usa um só fio, com bits codificados pela duração dos impulsos: o período de cada bit é de
1,25 µs, e um bit "0" ou "1" fica alto cerca de 0,4 µs ou 0,8 µs, com tolerância de ±150 ns. O
**RMT** (*Remote Control Transceiver*) é o periférico do ESP32 que gera sequências de impulsos com
temporização exata a partir de um buffer, sem o CPU. Usa-se através do componente oficial
`espressif/led_strip`, o caminho que a Espressif recomenda para estes LEDs. O ESP32-S3 tem 4 canais
RMT de transmissão, e só se usa um.

### LEDs simples

- São saídas push-pull, acesas no nível alto (LED com resistência de 220–330 Ω para GND). Para LEDs
  ligados ao 3,3 V existe a opção `ACTUATOR_LIGHT_ACTIVE_LOW`.
- O **pull-up interno** está ativo por omissão (`ACTUATOR_LIGHT_PULL_UP`). Fixa o nível do pino quando
  ele não é conduzido, por exemplo enquanto não há LED ligado. Enquanto é saída, o pino é sempre
  conduzido, e o pull-up custa só uns 70 µA com o pino em nível baixo.

### Testes

- O `actuator_controller` e o `device_listener` correm no QEMU com fakes do `rgb_led` e do
  `gpio_output`. Cobrem a cor (válida, inválida e por omissão), os erros do driver, o estado real e
  os eventos put/patch do campo `color`.
- O `gpio_output` real também corre no QEMU.
- O `rgb_led` real **não corre no QEMU**: o QEMU não emula o RMT e o `led_strip` fica à espera do fim
  da transmissão. É coberto pelos testes de integração na placa, que acendem cada saída e mostram
  vermelho, verde e azul no LED RGB.

## Alternativas consideradas

| Alternativa | Porque não |
|-------------|------------|
| Gerar o sinal do WS2812 por *bit-banging* num GPIO | Exige desligar interrupções durante a transmissão; as interrupções do Wi-Fi e os acessos à flash estragam a temporização. |
| Backend SPI do `led_strip` | Também cumpre a temporização (com DMA), mas ocupa um barramento SPI inteiro para um único LED; o RMT é o backend por omissão para poucos LEDs. |
| Brilho por PWM (LEDC) nos LEDs simples | O `light` só precisa de liga/desliga; fica para quando houver um atuador com intensidade. |
| Cor como objeto `{r, g, b}` ou número | `"#rrggbb"` é o formato do seletor de cor HTML, legível na consola e numa só escrita. |
| Publicar o estado pedido em vez do aplicado | A SPA não conseguiria distinguir "pedido" de "aplicado" quando o driver falha. |
| Os 3 LEDs da placa como luzes | Alimentação e TX/RX estão ligados ao hardware, não a GPIOs. |
| LEDs nos GPIO15–17 | Ocupados pela UART nesta montagem. |

## Consequências

**Positivas**

- Há atuadores reais de 2 tipos (`light` e `rgb_light`), com a cor a distinguir claramente o segundo.
- A temporização do WS2812 não depende do CPU nem do Wi-Fi.
- Mudar pinos, polaridade ou pull-up não exige mudar o código, e sem LEDs ligados o firmware continua
  a funcionar, com essas luzes simuladas.
- A SPA vê sempre o estado real das saídas.

**Negativas e limites**

- Nova dependência gerida: `espressif/led_strip` (^3.0, hoje 3.1.1).
- O driver real do LED RGB só se testa na placa.
- `light_1` e `light_2` não têm brilho; o brilho do `rgb_light` está embutido na cor.
- Atualizar o LED RGB bloqueia a task do stream durante a transmissão, ~30 µs para um LED.

## Atualização (2026-11-13): relés e cortina

Veja a [ADR 0005](0005-dispositivos-do-projeto.md) para o porquê. O catálogo passou a ter também:

| Id          | Tipo      | Saída                                                | `command`         |
|-------------|-----------|------------------------------------------------------|-------------------|
| `relay_1`   | `relay`   | módulo de relé em `CONFIG_ACTUATOR_RELAY_1_GPIO`     | `true` / `false`  |
| `relay_2`   | `relay`   | módulo de relé em `CONFIG_ACTUATOR_RELAY_2_GPIO`     | `true` / `false`  |
| `curtain_1` | `curtain` | dois relés: subir (GPIO18) e descer (GPIO21)         | `"up"`, `"down"`, `"stop"` |

- **Ativos a zero:** os módulos de relé acionam com nível baixo (`CONFIG_ACTUATOR_RELAY_ACTIVE_LOW`, por
  omissão ligado). O pull-up interno mantém-nos desligados enquanto o pino não é conduzido (arranque, reset),
  e o pino arranca já no nível desligado: `gpio_output_init()` ganhou o nível inicial, para o relé nunca
  receber um pulso baixo.
- **Um GPIO a `-1` deixa o relé simulado.** A `curtain_1` só fica real com os dois GPIOs definidos.
- **Interlock da cortina:** os dois relés nunca estão ligados ao mesmo tempo. Mudar de sentido desliga o
  relé que estava ativo, espera `CONFIG_ACTUATOR_CURTAIN_REVERSE_DELAY_MS` (500 ms) e só então liga o outro,
  para o motor parar antes de inverter. Parar não espera. Um comando em falta ou desconhecido vale `"stop"`,
  e um atuador desativado também pára a cortina.
- **Erro do driver:** se uma escrita falhar a meio de uma inversão, os dois relés são desligados e o estado
  publicado é `"stop"`.
- **Sem fim de curso nem tempo limite no firmware:** o motor fica ligado até chegar `"stop"`. Parte-se do
  princípio de que a cortina tem fins de curso. Se não tiver, falta um tempo máximo de funcionamento.
- **Onde corre:** o `set_curtain()` bloqueia durante a pausa de inversão, na task do `device_listener`.

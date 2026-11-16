# ADR 0003 — Sensores: temperatura interna do ESP32-S3 e LDR no ADC

- **Estado:** aceite
- **Data:** 2026-11-10
- **Âmbito:** dispositivo IoT; o formato dos dados segue a [ADR 0001](0001-modelo-de-dados.md)

## Contexto

O enunciado exige pelo menos 2 tipos de sensores e pelo menos um sensor real. Até aqui, todos os
sensores (temperatura, humidade, luminosidade e movimento) eram simulados. O ESP32-S3 tem um
**sensor de temperatura interno**, que não precisa de hardware nem de ligações extra.

Segundo a documentação do ESP-IDF v6.1 (*Temperature Sensor*), este sensor mede a temperatura **do
silício**. Acompanha bem as variações, mas não dá um valor exato do ambiente: lê acima da temperatura
do ar, sobretudo com o Wi-Fi ativo. Sai do intervalo configurado com `ESP_FAIL`.

## Decisão

| Id              | Tipo          | Origem                                        | `simulated` |
|-----------------|---------------|-----------------------------------------------|-------------|
| `temperature_1` | `temperature` | sensor interno do ESP32-S3 (temperatura do chip) | `false`  |
| `temperature_2` | `temperature` | LM35 no GPIO5 (ADC1): ver «Atualização» no fim | `false` |
| `luminosity_1`  | `luminosity`  | LDR num divisor de tensão no GPIO4 (ADC1, canal 3) | `false` |
| `luminosity_2`  | `luminosity`  | segundo LDR (`CONFIG_LDR_2_GPIO`); simulado por omissão (-1) | `true` |
| `gas_1`         | `gas`         | MQ-135 no GPIO8 (ADC1): ver «Atualização: MQ-135» no fim | `false` |

- **HAL `chip_temperature`:** um wrapper fino sobre `driver/temperature_sensor.h` (componente
  `esp_driver_tsens`). Instala e ativa o sensor no `init` e lê em graus Celsius. Nos testes é
  substituído por um fake.
- **Intervalo de 10 a 40 °C:** o driver escolhe a faixa de hardware mais precisa que o cubra.
- **O `sensor_provider` passa a ter `init`/`deinit`,** porque o sensor real é um recurso.
- **Um sensor real só é lido quando está ativo** (`enabled`).
- **Se uma leitura falhar,** por exemplo com o chip fora dos 10–40 °C, fica registado um aviso e o
  valor vira NaN. O JSON deixa de fora os valores não finitos, por isso os outros sensores continuam a
  ser publicados.
- **O driver não tem lock,** por isso o sensor só é lido pela task principal, a que publica.

### Luminosidade: LDR no ADC

- **Circuito:** um LDR e uma resistência fixa formam um divisor de tensão entre 3,3 V e GND, com o
  ponto médio no GPIO4. Por omissão (Kconfig), o LDR fica do lado dos 3,3 V e uma resistência de
  10 kΩ do lado de GND, por isso a tensão sobe com a luz. A ligação, a resistência fixa e os valores
  do LDR (resistência a 10 lux e gama da datasheet) configuram-se no `menuconfig`.
- **Porquê o GPIO4:** é o canal 3 do **ADC1**. O ADC2 não pode ser lido com o Wi-Fi ativo.
- **HAL `analog_input`:** um wrapper fino sobre o driver **one-shot** do ADC (`esp_adc`), com
  atenuação de 12 dB (cerca de 0–3,1 V), e sobre o **driver de calibração** do ADC (esquema *curve
  fitting* do ESP32-S3, a partir dos coeficientes do eFuse). Sem esses eFuses, as leituras usam uma
  conversão linear aproximada e fica um aviso no log.
- **Porquê one-shot:** basta uma conversão a cada publicação (5 s). O modo contínuo (DMA) serve para
  amostragem rápida e só ocuparia memória e um periférico sem ganho.
- **De tensão para lux:** obtém-se R_LDR a partir do divisor, e lux = 10 × (R10 / R_LDR)^(1/γ). Um LDR
  não é um luxímetro calibrado: o valor dá a ordem de grandeza e acompanha bem as variações. Uma
  tensão impossível (fora de 0–3,3 V) ou um erro de leitura deixa o valor de fora, como no sensor
  do chip.

## Alternativas consideradas

| Alternativa | Porque não |
|-------------|------------|
| Sensor externo (DHT22, BME280…) | Exige hardware e ligações que ainda não existem; pode entrar depois como mais um sensor do catálogo. |
| Corrigir a leitura do chip para "temperatura ambiente" com um desvio fixo | O desvio muda com a carga e o Wi-Fi; um valor corrigido pareceria exato sem o ser. |
| Substituir o sensor simulado pelo real (um só sensor de temperatura) | Ter os dois permite comparar o real com o simulado, e a SPA continua a ter uma temperatura "de sala". |
| Faixa de -10 a 80 °C | Cobriria um chip quente, mas com mais erro; 10–40 °C foi a faixa pedida. |
| ADC em modo contínuo (DMA) | Para uma leitura a cada 5 s, o one-shot é mais simples e não ocupa DMA nem buffers. |
| Ler o ADC sem calibração (valor bruto) | O bruto não é linear nas pontas e varia entre chips; a calibração do eFuse dá milivolts consistentes. |
| Publicar milivolts ou percentagem em vez de lux | O lux aproximado é o que faz sentido na SPA e nas regras ("luminosidade inferior a X"); os mV ficam no log de debug. |

## Consequências

- Há dois sensores reais, de tipos diferentes: `temperature_1` e `luminosity_1`.
- A SPA não deve tratar `temperature_1` como temperatura da sala. Uma etiqueta (por exemplo,
  "Temperatura do chip") ajuda a deixá-lo claro.
- Se o chip passar dos 40 °C, as leituras de `temperature_1` deixam de aparecer até a temperatura
  voltar a descer. Fica um aviso no log a cada leitura falhada.
- O ciclo de vida do `chip_temperature` é testado no QEMU, e as leituras reais nos testes de
  integração na placa. O `analog_input` não corre no QEMU (o arranque fica preso na calibração do
  ADC). A conversão para lux e o tratamento de erros são testados com o fake.

## Atualização (2026-11-13): `temperature_2` passa a ser um LM35

O LM35 (sensor analógico, 10 mV por °C) mede a temperatura **do ar**, ao contrário do sensor do chip, e
substitui o mock de `temperature_2`. O `temperature_1` continua a ser o do chip.

- **Ligação:** saída no **GPIO5** (ADC1, canal 4, ao lado do LDR no GPIO4). O LM35 precisa de pelo menos
  4 V: alimenta-se a 5 V (pino 5V da placa). Com 5 V de alimentação a saída chega a 1,5 V aos 150 °C, dentro
  do alcance do ADC (cerca de 0–3,1 V a 12 dB).
- **Conversão:** °C = mV / 10. Uma tensão fora de 0–1500 mV (0–150 °C, a gama com alimentação simples) ou um
  erro de leitura deixa o valor de fora, como no LDR.
- **HAL `analog_input` com várias entradas:** passa a devolver um *handle* opaco, com um pool estático de
  até 4 entradas que partilham a unidade ADC1. Um GPIO do ADC2 é recusado (`ESP_ERR_NOT_SUPPORTED`), porque o
  ADC2 não se lê com o Wi-Fi ativo.
- **`CONFIG_LM35_GPIO = -1`** deixa `temperature_2` simulada, como o `CONFIG_LDR_GPIO = -1` faz com a
  luminosidade.
- **Limitação:** o ADC do ESP32-S3 tem ruído da ordem de décimos de grau neste sensor. Se vier a pesar, a
  média de várias amostras cabe no `sensor_provider` sem mudar o HAL.

## Atualização (2026-11-13): `gas_1`, um MQ-135

Sensor de qualidade do ar (NH3, NOx, CO2, benzeno). O módulo tem saída analógica (AO) e um aquecedor.

- **Ligação:** AO no **GPIO8** (ADC1, canal 7) **através de um divisor**, porque a AO chega a 5 V e o ADC
  aguenta cerca de 3,1 V. Por omissão 10 kΩ (AO → GPIO) e 20 kΩ (GPIO → GND): o pino vê 2/3 da AO. Um
  condensador de 100 nF no pino estabiliza a leitura com esta impedância. O módulo alimenta-se a 5 V.
- **Conversão:** Rs = RL × (5 V / V_AO − 1), com V_AO = V_pino × 1,5, e depois a curva de CO2
  ppm = 116,6 × (Rs/R0)^-2,77. RL (carga do módulo), R0 (Rs em ar limpo) e o divisor configuram-se no
  `menuconfig`. A tensão da AO fora de (0, 5 V) deixa o valor de fora; acima de 10 000 ppm o valor satura.
- **Unidade:** `ppm`, CO2 equivalente. Um MQ-135 reage a vários gases e deriva com a temperatura, a humidade
  e a idade: o valor dá a ordem de grandeza e a tendência, não uma medição calibrada.
- **Calibração:** com 24 h de pré-aquecimento e ar limpo (cerca de 400 ppm), copie o `gas_1: Rs` do log de
  debug do `sensor_provider` para `MQ135_R0_OHMS`. O valor por omissão (76,6 kΩ) é só típico.
- **`CONFIG_MQ135_GPIO = -1`** deixa o `gas_1` simulado (400–1000 ppm).
- **Limites do catálogo:** com mais um sensor, `SENSOR_READINGS_JSON_MAX_LEN` passou de 256 para 384 bytes.

## Atualização (2026-11-13): catálogo do projeto

Com a [ADR 0005](0005-dispositivos-do-projeto.md) o catálogo ficou só com o que o projeto usa:
`temperature_1`, `temperature_2`, `luminosity_1`, `luminosity_2` e `gas_1`.

- **Removidos** `humidity_1` e `motion_1`, que eram simulados e ninguém usava. Com eles saiu o suporte a
  sensores booleanos (`sensor_info_t::boolean`), que deixou de ter quem o use.
- **Novo `luminosity_2`:** um segundo LDR com os mesmos valores de divisor, no pino `CONFIG_LDR_2_GPIO`. Fica
  simulado (0–1000 lx) enquanto esse pino for -1.

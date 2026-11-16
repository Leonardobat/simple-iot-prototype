# Documentação, legibilidade e nomes


- **Doxygen** em todo `.h`: `@file`, `@brief` por módulo e por símbolo público, `@param[in|out]`, `@return`
  (listando cada `ESP_ERR_*`), `@pre`, `@post`, `@note` (thread-safety, ISR-safety, bloqueante ou não, ownership de memória).
- Comentários explicam o **porquê**, não o quê; nomes autoexplicativos dispensam comentário óbvio.
- Funções curtas, uma responsabilidade; ≤ ~40 linhas e ≤ 4 parâmetros como guia (agrupe em `struct` de config se passar).
- Constantes, pinos, timeouts e tamanhos centralizados num único header de configuração por componente
  (`private_include/<componente>_config.h`) ou `Kconfig`; limites que fazem parte do contrato (ex.:
  `HTTP_CLIENT_URL_MAX_LEN`) ficam no header público. Nada de literais espalhados.

### Convenção de nomes

| Elemento                   | Estilo                     | Exemplo                          |
|----------------------------|----------------------------|----------------------------------|
| Funções e variáveis        | `snake_case`               | `sensor_read_temperature`        |
| Funções públicas           | `<modulo>_<acao>`          | `data_repository_save`           |
| Constantes e macros        | `UPPER_CASE`               | `MAX_RETRIES`                    |
| Tipos / structs / enums    | sufixo `_t` (enum valores `UPPER_CASE` com prefixo do módulo) | `sensor_config_t`, `SENSOR_STATE_IDLE` |
| Globais de arquivo         | `static` + prefixo `s_`    | `s_mutex`                        |
| Ponteiros opacos           | `<modulo>_handle_t`        | `http_client_handle_t`           |

Formatação: indentação 4 espaços, sem tabs, linhas ≤ 100 colunas, `clang-format` com configuração única no repositório
(`.clang-format`): `make format` aplica, `make format-check` verifica.


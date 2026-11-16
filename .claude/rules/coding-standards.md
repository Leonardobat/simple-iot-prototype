# Padrões de código (MISRA C:2012, subconjunto estrito)


- **C11** no código do projeto (o ESP-IDF 6.1 compila com `-std=gnu23`; evite extensões GNU).
- **Compilação estrita** nos componentes do projeto (não nos `managed_components`/IDF), centralizada em
  `cmake/strict_warnings.cmake` (`iot_enable_strict_warnings()` logo após `project()`): `-Wall -Wextra -Werror
  -Werror=extra -Wsign-compare -Wunused-parameter -Wshadow -Wformat=2` e os `-Werror=unused-*`.
  **`-Wpedantic` não é usado**: os próprios headers do ESP-IDF 6.1 falham (`#include_next`, `struct` vazia em
  `sys/reent.h`, `__FUNCTION__` em `esp_check.h`).
  Nunca silenciar um aviso com `#pragma`/cast sem justificativa em comentário.
- **Tipos**: somente `<stdint.h>`/`<stdbool.h>`/`<stddef.h>` (`uint8_t`, `int32_t`, `size_t`, `bool`).
  Proibido `int`/`long`/`char` para dados numéricos. `char` só para texto.
- **`const`** em todo ponteiro/dado que não é modificado (`const char *`, `const uint8_t *`, tabelas `static const`).
- **Memória**:
  - Alocação **estática** ou buffers/pools pré-alocados; sem `malloc`/`free` em laços ou tarefas periódicas.
  - Alocação dinâmica só na inicialização (`*_init`), com retorno verificado e liberação simétrica em `*_deinit`.
    Recursos de biblioteca (ex.: `esp_http_client`) criados uma vez no init e reutilizados por request.
  - Buffers de tamanho fixo definidos por constante nomeada; use `snprintf`, nunca `sprintf`/`strcpy`/`strcat`/`gets`.
  - Evite VLAs e `alloca`. Cuidado com tamanho de stack das tarefas FreeRTOS (documente e valide `uxTaskGetStackHighWaterMark`).
- **Proibido**: recursão, `goto` (exceto cleanup único e documentado), `setjmp/longjmp`, `union` para type-punning,
  aritmética de ponteiros fora de arrays, macros com efeitos colaterais, números mágicos, `static` mutável global sem dono claro.
- **Controle de fluxo**: todo `if/else if` termina em `else` ou comentário explicando; todo `switch` tem `default`;
  chaves `{}` sempre; no máximo 3 níveis de indentação (use guard clauses / early return); uma instrução por linha.
- **Conversões** explícitas e justificadas; sem comparações signed/unsigned implícitas; sem *shift* em valores com sinal.
- **Concorrência**: estado compartilhado protegido por mutex/queue/event group; ISR usa apenas APIs `*FromISR` e `IRAM_ATTR`;
  nada de `printf`/malloc/bloqueio em ISR.
- **Análise estática**: `make lint` (`idf.py clang-check` com o `.clang-tidy` do projeto; checks desligados
  estão justificados no próprio arquivo). `cppcheck --addon=misra` quando o cppcheck estiver instalado;
  desvios do MISRA ficam registrados com comentário `/* MISRA deviation: Rule X.Y — motivo */`.


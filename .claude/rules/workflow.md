# Fluxo de trabalho e checklist (iot_device, ESP32-S3, ESP-IDF, C)

Prioridades: **robustez > legibilidade > modularidade > desempenho**. Complementa o `~/.claude/CLAUDE.md` (KISS, DRY, YAGNI); em conflito vale a regra mais estrita.

## Fluxo
1. Explore o código existente (`components/`, `test/`) antes de alterar; reutilize antes de criar.
2. Mudanças pequenas e incrementais; cada mudança acompanha seu teste.
3. Mudanças arquiteturais (novo componente, mudança de camada, nova dependência) exigem confirmação prévia.
4. Ao terminar: `make check` passando (format-check, lint, build sem avisos, testes unitários em QEMU). Relate falhas fielmente.

## Checklist antes de concluir uma tarefa

- [ ] `make check` passa (avisos estritos de `cmake/strict_warnings.cmake`, clang-tidy, clang-format, testes)
- [ ] Nenhum retorno de função ignorado; parâmetros externos validados
- [ ] Sem `malloc`/`free` fora de init/deinit; sem tipos `int`/`long` nus
- [ ] Header com Doxygen completo (pré/pós-condições, erros)
- [ ] Hardware acessado só via HAL; testes unitários com mocks passam no QEMU
- [ ] Testes de integração (se aplicável) escritos e instruções entregues ao usuário
- [ ] Sem duplicação (DRY) e sem código "por precaução" (YAGNI)

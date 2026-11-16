# Algoritmo do Banqueiro

Implementação em C do algoritmo do Banqueiro. O programa lê as demandas máximas de customer.txt, processa os comandos RQ, RL e * de commands.txt e grava a saída em result.txt.

## Sistema

O programa foi implementado e testado no Ubuntu pelo WSL2, com o compilador GCC.

## Compilação

Na raiz do projeto, execute make. O Makefile gera o executável banker no mesmo diretório. O comando make clean remove o executável e o arquivo result.txt.

## Execução

Coloque customer.txt e commands.txt na raiz do projeto. Cada argumento informa a quantidade disponível de um tipo de recurso. Para três recursos com 10, 5 e 7 instâncias, execute ./banker 10 5 7. O programa grava o resultado em result.txt.

## Testes

Compile com make, execute o programa com as quantidades de recursos correspondentes ao customer.txt e confira result.txt. Para o exemplo fornecido, a comparação pode ser feita com cmp result.txt resultado_esperado.txt. Também foram testados arquivos ausentes ou malformados, comandos desconhecidos, clientes inexistentes, quantidade incorreta de recursos e liberações maiores do que a alocação.

## Arquivos da entrega

- banker.c: leitura, validação, alocação, liberação, teste de segurança e escrita da saída.
- Makefile: compilação e limpeza dos arquivos gerados.
- evidencias.log: histórico.
- hmrn.pdf: relatório técnico.

Os arquivos customer.txt, commands.txt e result.txt fornecidos pelo professor não fazem parte da entrega.

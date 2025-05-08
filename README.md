# Compilação e Execução

## Requisitos:
- GCC
- Linux com suporte a pthread
- Arquivo `devices.csv` no mesmo diretório do código fonte

## Compilar:
Estando com o terminal na pasta do código, use o seguinte comando: gcc -o Code Code.c -pthread

## Executar:
No mesmo terminal, executar o seguinte comando:
./Code

Ao final, será gerado o arquivo `resumo.csv` com os resultados.

# Formato do CSV de entrada (`devices.csv`)
O arquivo de entrada deve conter dados separados por pipe (`|`) no seguinte formato:

id|device|contagem|data|temperatura|umidade|luminosidade|ruido|eco2|etvoc|latitude|longitude

- O programa ignora os registros anteriores a março de 2024.

# Distribuição das Cargas entre as Threads
- O programa detecta automaticamente o número de núcleos de CPU disponíveis via `sysconf(_SC_NPROCESSORS_ONLN)` e cria uma thread por núcleo.
- Os registros válidos são divididos uniformemente por quantidade entre as threads.
- Cada thread recebe um intervalo de índices do vetor de registros.

# Análise dos Dados pelas Threads
- Cada thread percorre seu intervalo de registros e, para cada sensor em cada linha:
  - Identifica (por dispositivo, ano, mês e sensor) se já existe uma entrada estatística local.
  - Se existir, atualiza mínimo, máximo, soma e contagem.
  - Se não existir, cria uma nova entrada no seu mapa local de estatísticas.
- Ao final, cada thread retorna seu mapa com estatísticas mensais por sensor.

# Geração do CSV de Resultados
- Após o término das threads, a `main` consolida os mapas locais das threads em um mapa global:
  - Se uma combinação (dispositivo, ano, mês, sensor) já existe no global, os valores são combinados.
  - Caso contrário, a entrada local é copiada diretamente para o mapa global.
- O programa então gera o arquivo `resumo.csv` com o seguinte formato:

dispositivo;ano-mes;sensor;maximo;media;minimo

# Tipo de Execução das Threads
As threads são criadas com a biblioteca POSIX (`pthread`), o que no Linux significa que cada `pthread` é implementada como uma kernel thread. Isso permite que o escalonador do sistema operacional distribua as threads entre os núcleos físicos de forma nativa.

# Possíveis Problemas de Concorrência
## Compartilhamento indireto de memória
- Cada thread trabalha com um vetor global de registro, acessando os elementos por índice. No caso só esta sendo feito leitura mas é importante garantir que nenhuma outra thread ou função esteja modificando esse vetor durante a execução paralela e os dados estejam totalmente carregados antes das pthread_create.

## Acesso à saída padrão
- Tanto printf quanto fprintf são chamadas não sincronizadas. Se mais de uma thread escrevesse nelas ao mesmo tempo, poderia ocorrer uma mistura na saída.

# Observações Finais
- Ideal para processar grandes quantidades de dados de sensores.
- O uso de paralelismo melhora o desempenho em sistemas com múltiplos núcleos.
- Código extensível para incluir novos tipos de sensores ou formatos de entrada.

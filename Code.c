#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#define TAM_LINHA   512
#define TAM_DISPOSITIVO 64
#define QTD_SENSORES 6

typedef struct {
    char dispositivo[TAM_DISPOSITIVO];
    int ano, mes;
    double sensores[QTD_SENSORES];
} Registro;

typedef struct {
    char dispositivo[TAM_DISPOSITIVO];
    int ano, mes, sensor_id;
    double minimo, maximo, soma;
    size_t contagem;
} Estatistica;

typedef struct {
    Estatistica *elementos;
    size_t tamanho, capacidade;
} MapaEstatisticas;

typedef struct {
    Registro *registros;
    size_t inicio, fim;
    MapaEstatisticas mapa_local;
} ArgumentoThread;

const char *nomes_sensores[QTD_SENSORES] = {
    "temperatura", "umidade", "luminosidade",
    "ruido", "eco2", "etvoc"
};

void *alocar_memoria(size_t tam) {
    void *p = malloc(tam);
    if (!p) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    return p;
}

void *realocar_memoria(void *antigo, size_t tam) {
    void *p = realloc(antigo, tam);
    if (!p) {
        perror("realloc");
        exit(EXIT_FAILURE);
    }
    return p;
}

void inicializar_mapa(MapaEstatisticas *m) {
    m->tamanho = 0;
    m->capacidade = 1024;
    m->elementos = alocar_memoria(m->capacidade * sizeof(Estatistica));
}

void liberar_mapa(MapaEstatisticas *m) {
    free(m->elementos);
}

void garantir_capacidade(MapaEstatisticas *m) {
    if (m->tamanho >= m->capacidade) {
        m->capacidade *= 2;
        m->elementos = realocar_memoria(m->elementos,
                                         m->capacidade * sizeof(Estatistica));
    }
}

ssize_t buscar_estatistica(MapaEstatisticas *m,
                           const char *disp,
                           int ano, int mes,
                           int sensor_id) {
    for (size_t i = 0; i < m->tamanho; i++) {
        Estatistica *e = &m->elementos[i];
        if (e->sensor_id == sensor_id &&
            e->ano == ano && e->mes == mes &&
            strcmp(e->dispositivo, disp) == 0) {
            return i;
        }
    }
    return -1;
}

void atualizar_mapa(MapaEstatisticas *m,
                    const char *disp,
                    int ano, int mes,
                    int sensor_id, double valor) {
    ssize_t idx = buscar_estatistica(m, disp, ano, mes, sensor_id);
    if (idx < 0) {
        garantir_capacidade(m);
        Estatistica *e = &m->elementos[m->tamanho++];
        strncpy(e->dispositivo, disp, TAM_DISPOSITIVO - 1);
        e->dispositivo[TAM_DISPOSITIVO - 1] = '\0';
        e->ano = ano;
        e->mes = mes;
        e->sensor_id = sensor_id;
        e->minimo = e->maximo = valor;
        e->soma = valor;
        e->contagem = 1;
    } else {
        Estatistica *e = &m->elementos[idx];
        if (valor < e->minimo) e->minimo = valor;
        if (valor > e->maximo) e->maximo = valor;
        e->soma += valor;
        e->contagem++;
    }
}

void *funcao_thread(void *arg) {
    ArgumentoThread *arg_thread = arg;
    inicializar_mapa(&arg_thread->mapa_local);
    for (size_t i = arg_thread->inicio; i < arg_thread->fim; i++) {
        Registro *reg = &arg_thread->registros[i];
        for (int s = 0; s < QTD_SENSORES; s++) {
            atualizar_mapa(&arg_thread->mapa_local,
                           reg->dispositivo, reg->ano, reg->mes,
                           s, reg->sensores[s]);
        }
    }
    return NULL;
}

int main() {
    FILE *arquivo = fopen("devices.csv", "r");
    if (!arquivo) {
        perror("devices.csv");
        return 1;
    }

    char linha[TAM_LINHA];

    if (!fgets(linha, TAM_LINHA, arquivo)) {
        fprintf(stderr, "Erro ao ler cabeçalho\n");
        fclose(arquivo);
        return 1;
    }

    Registro *registros = NULL;
    size_t capacidade = 0, total = 0;
    struct tm data_tm;

    while (fgets(linha, TAM_LINHA, arquivo)) {
        char *tok = strtok(linha, "|");
        if (!tok) continue;

        tok = strtok(NULL, "|");
        if (!tok) continue;

        char disp[TAM_DISPOSITIVO];
        strncpy(disp, tok, TAM_DISPOSITIVO - 1);
        disp[TAM_DISPOSITIVO - 1] = '\0';

        tok = strtok(NULL, "|");
        if (!tok) continue;

        tok = strtok(NULL, "|");
        if (!tok) continue;

        char data_str[11];
        strncpy(data_str, tok, 10);
        data_str[10] = '\0';
        memset(&data_tm, 0, sizeof(data_tm));

        if (!strptime(data_str, "%Y-%m-%d", &data_tm)) continue;

        int ano = data_tm.tm_year + 1900;
        int mes = data_tm.tm_mon + 1;
        if (ano < 2024 || (ano == 2024 && mes < 3)) continue;

        if (total >= capacidade) {
            capacidade = capacidade ? capacidade * 2 : 1024;
            registros = realocar_memoria(registros, capacidade * sizeof(Registro));
        }

        Registro *reg = &registros[total++];
        strncpy(reg->dispositivo, disp, TAM_DISPOSITIVO - 1);
        reg->dispositivo[TAM_DISPOSITIVO - 1] = '\0';
        reg->ano = ano;
        reg->mes = mes;

        int valido = 1;
        for (int i = 0; i < QTD_SENSORES; i++) {
            tok = strtok(NULL, "|");
            if (!tok) {
                fprintf(stderr, "Linha %zu: sensor %d ausente, ignorando registro\n", total, i);
                valido = 0;
                break;
            }
            reg->sensores[i] = atof(tok);
        }
        if (!valido) {
            total--;
            continue;
        }
    }

    fclose(arquivo);

    if (total == 0) {
        fprintf(stderr, "Nenhum registro válido após 2024-03.\n");
        free(registros);
        return 1;
    }

    int qtd_threads = sysconf(_SC_NPROCESSORS_ONLN);
    if (qtd_threads < 1) qtd_threads = 1;

    pthread_t threads[qtd_threads];
    ArgumentoThread argumentos[qtd_threads];
    size_t bloco = total / qtd_threads;

    for (int i = 0; i < qtd_threads; i++) {
        argumentos[i].registros = registros;
        argumentos[i].inicio = i * bloco;
        argumentos[i].fim = (i == qtd_threads - 1 ? total : (i + 1) * bloco);
        pthread_create(&threads[i], NULL, funcao_thread, &argumentos[i]);
    }

    MapaEstatisticas mapa_global;
    inicializar_mapa(&mapa_global);
    for (int i = 0; i < qtd_threads; i++) {
        pthread_join(threads[i], NULL);
        MapaEstatisticas *local = &argumentos[i].mapa_local;
        for (size_t j = 0; j < local->tamanho; j++) {
            Estatistica *e = &local->elementos[j];
            ssize_t idx = buscar_estatistica(&mapa_global,
                                             e->dispositivo, e->ano, e->mes,
                                             e->sensor_id);
            if (idx < 0) {
                garantir_capacidade(&mapa_global);
                mapa_global.elementos[mapa_global.tamanho++] = *e;
            } else {
                Estatistica *g = &mapa_global.elementos[idx];
                if (e->minimo < g->minimo) g->minimo = e->minimo;
                if (e->maximo > g->maximo) g->maximo = e->maximo;
                g->soma += e->soma;
                g->contagem += e->contagem;
            }
        }
        liberar_mapa(local);
    }

    FILE *saida = fopen("resumo.csv", "w");
    if (!saida) {
        perror("resumo.csv");
        return 1;
    }

    printf("Resultado:\n");
    fprintf(saida, "dispositivo;ano-mes;sensor;maximo;media;minimo\n");
    printf("dispositivo;ano-mes;sensor;maximo;media;minimo\n");

    for (size_t i = 0; i < mapa_global.tamanho; i++) {
        Estatistica *e = &mapa_global.elementos[i];
        double media = e->soma / e->contagem;

        char linha_saida[500];
        snprintf(linha_saida, sizeof linha_saida,
                 "%s;%04d-%02d;%s;%.2f;%.2f;%.2f\n",
                 e->dispositivo, e->ano, e->mes,
                 nomes_sensores[e->sensor_id],
                 e->maximo, media, e->minimo);

        fprintf(saida, "%s", linha_saida);
        printf("%s", linha_saida);
    }

    fclose(saida);
    liberar_mapa(&mapa_global);
    free(registros);
    return 0;
}

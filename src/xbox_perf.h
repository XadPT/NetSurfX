/* NetSurfX - diagnostico: desempenho, memoria, registo e vigilante */
#ifndef NSX_XBOX_PERF_H
#define NSX_XBOX_PERF_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* relogio em microssegundos (da a volta: usar so diferencas) */
uint32_t nsx_perf_us(void);

/* memoria fisica (KB) */
uint32_t nsx_mem_free_kb(void);
uint32_t nsx_mem_total_kb(void);

/* registo (NETSURF.LOG): ficheiro, escrita periodica e limite de tamanho */
void nsx_log_set_fp(FILE *fp);
void nsx_log_tick(void);          /* thread principal, a cada volta */
void nsx_log_flush(void);

/* contadores */
void nsx_perf_fetch_done(uint32_t ms, uint32_t bytes, bool ok);
void nsx_perf_redraw(uint32_t ms);
void nsx_perf_present(uint32_t us);
void nsx_perf_page_start(const char *url);
void nsx_perf_page_done(const char *url);
/* linha curta para a barra de estado (estatisticas no ecra) */
void nsx_perf_status(char *buf, size_t len);

/* vigilante: deteta bloqueios da thread principal (> 8 s) e escreve-os em
 * D:\NSX-BLOQUEIO.LOG a partir de uma thread propria */
void nsx_watch_start(void);
void nsx_heartbeat(const char *phase);

/* quantos asserts falharam (registados em NSX-ERRO.LOG) */
unsigned nsx_assert_count(void);

#endif

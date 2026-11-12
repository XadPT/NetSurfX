# NetSurfX

O browser [NetSurf](https://www.netsurf-browser.org/) 3.11 para a Xbox clássica, feito com o [nxdk](https://github.com/XboxDev/nxdk).

Instruções para quem joga: `dist/NetSurfX/LEIA-ME.txt`.

## O que tem

- Motor completo do NetSurf: HTML, CSS, imagens PNG/JPEG/GIF/BMP, formulários, cookies, histórico.
- Fetcher próprio (`src/fetch_xbox.c`) em vez do curl: uma thread por pedido sobre lwIP, HTTPS com
  mbedTLS 3.6 (TLS 1.2 e 1.3, certificados verificados, página de aviso com "Proceed"), gzip,
  chunked, redireccionamentos, POST, retoma de sessões TLS.
- Superfície libnsfb própria (`src/nsfb_xbox.c`): desenha em RAM e copia só a zona alterada
  para o ecrã, uma vez por ciclo; margem de overscan; 720p opcional.
- Comando: ponteiro no analógico esquerdo, scroll suave no direito, teclado no ecrã
  (barra de endereço e campos das páginas), ajuda, favoritos, zoom, IGR.
- Rede em segundo plano com a configuração do dashboard (DNS manual incluído).

## Lições aproveitadas de outros portes

- 3DS: o separador `:` dos caminhos de recursos parte com letras de unidade -> caminhos fixos;
  fontes `cursive`/`fantasy` têm de existir -> todas apontam para DejaVu; actualizar o ecrã a
  cada pequena mudança é lento -> apresentação agrupada.
- Switch: nunca emitir movimentos do ponteiro sem mudança de posição (o ciclo do fbtk não
  termina); clicar sempre na posição actual do ponteiro; teclado que abre quando o NetSurf
  põe o cursor de texto num campo, depois de largado o botão.

## Compilar

Pastas lado a lado:

    nxdk/                      git clone --recursive https://github.com/XboxDev/nxdk.git
    netsurf-all-3.11/          https://download.netsurf-browser.org/netsurf/releases/source-full/
    mbedtls-3.6.2/             https://github.com/Mbed-TLS/mbedtls/releases
    netsurfx/                  este projecto

1. `python3 tools/patch_netsurf.py <netsurf-all-3.11>` (alterações mínimas, idempotentes).
2. Compilar as bibliotecas do NetSurf nativamente uma vez (gera ficheiros automáticos) e depois
   `tools/build_libs.py`, `tools/build_mbedtls.py` e `tools/build_ns.py` com o ambiente do nxdk
   activo (`eval "$(../nxdk/bin/activate -s)"`). Os caminhos no topo dos scripts têm de ser ajustados.
3. `make` -> `bin/default.xbe`. Copiar para `dist/NetSurfX/` (ao lado de `res/`).

O `gperf` não é preciso: `tools/gen_element_type.py` gera a tabela de elementos do hubbub.

## Testes no PC

`tools/hosttest/` compila exactamente o mesmo código para Linux com o hardware simulado
(ecrã em memória com capturas PNG, comando controlado por guião, sockets POSIX) e um servidor
local HTTP/HTTPS (`srv/server.py`). Há variantes com AddressSanitizer e ThreadSanitizer.

## Licença

NetSurf e este port: GPLv2. libnsfb e a superfície Xbox: MIT. mbedTLS: Apache 2.0.
Fontes DejaVu: licença própria (livre). Ver `dist/NetSurfX/licencas/`.

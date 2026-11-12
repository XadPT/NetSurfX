# NetSurfX

Browser NetSurf 3.11 para a Xbox classica (nxdk).

## Estado
- Fase 1: janela SDL, rede lwIP, cursor com analogico  [feito]
- Fase 2: libs do NetSurf compiladas para nxdk e ligadas [feito, por testar na consola]
  - libwapcaplet, libparserutils (sem iconv), libcss, libhubbub, libdom (sem expat), libnsutils (parcial)
  - teste: barra 2 azul = o parse HTML (hubbub+dom) devolveu 2 <p>; laranja = falhou
  - tools/gen_element_type.py substitui o gperf (tabela de elementos do hubbub)
  - tools/build_libs.py recompila as libs com nxdk-cc (precisa das libs ja compiladas
    no PC com o buildsystem do NetSurf para gerar ficheiros automaticos)
- Fase 3: HTTP/HTTPS [feito, por testar na consola]
  - src/nethttp.c: cliente GET minimo sobre lwIP + mbedTLS 3.6.2 (redirecionamentos, chunked)
  - mbedTLS compilado com tools/build_mbedtls.py (config em mbedtls_cfg/)
  - certificados: assets/cacert.pem (copiar para a mesma pasta do default.xbe). Vem do pacote
    ca-certificates do Debian (derivado do Mozilla); a verificacao e obrigatoria
  - relogio: se o RTC da Xbox estiver antes da data de compilacao, usa a data de compilacao
  - entropia: jitter do rdtsc (a Xbox nao tem RNG de hardware) - fraca, aceitavel para testes
- Fase 4: frontend NetSurf (framebuffer/libnsfb ou frontend proprio SDL), teclado no ecra

## Compilar
  git clone --recursive https://github.com/XboxDev/nxdk.git   (pasta ao lado)
  cd nxdk && eval "$(./bin/activate -s)"
  cd ../netsurfx && make

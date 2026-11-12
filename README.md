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
- Fase 3: HTTP (libcurl ou cliente proprio sobre lwIP), depois HTTPS (mbedTLS)
- Fase 4: frontend NetSurf (framebuffer/libnsfb ou frontend proprio SDL), teclado no ecra

## Compilar
  git clone --recursive https://github.com/XboxDev/nxdk.git   (pasta ao lado)
  cd nxdk && eval "$(./bin/activate -s)"
  cd ../netsurfx && make

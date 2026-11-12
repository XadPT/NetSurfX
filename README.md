# NetSurfX - fase 1

Estrutura:
  nxdk/        (git clone --recursive https://github.com/XboxDev/nxdk.git)
  netsurfx/    (esta pasta)

Compilar (com a toolchain nxdk ja configurada):
  cd nxdk && eval $(./bin/activate -s)
  cd ../netsurfx && make

Resultado: bin/default.xbe e NetSurfX.iso. Testar no xemu ou na consola.
Esperado: ecra claro, barra verde no topo (rede OK), quadrado preto que
se move com o analogico esquerdo; Start sai.

Fase 2: compilar libparserutils, libwapcaplet, libcss, libdom, libhubbub
com nxdk e abrir uma pagina HTML local com o frontend NetSurf.

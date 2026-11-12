// NetSurfX: janela SDL 640x480, cursor, diagnostico de rede e teste de HTML.
#include <SDL.h>
#include <hal/video.h>
#include <hal/debug.h>
#include <hal/xbox.h>
#include <windows.h>
#include <nxdk/net.h>
#include <nxdk/configsector.h>
#include <lwip/netif.h>
#include <lwip/ip4_addr.h>
#include <lwip/dns.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

int nstest_parse(void);
int nstest_count_tag(const char *html, size_t html_len, const char *tagname);
#include "nethttp.h"
#define W 640
#define H 480

// fonte 3x5: digitos e algumas letras
static const struct { char c; const char *rows; } glyphs[] = {
    {'0',"111101101101111"},{'1',"010110010010111"},{'2',"111001111100111"},
    {'3',"111001111001111"},{'4',"101101111001001"},{'5',"111100111001111"},
    {'6',"111100111101111"},{'7',"111001010010010"},{'8',"111101111101111"},
    {'9',"111101111001111"},{'.',"000000000000010"},{'-',"000000111000000"},
    {':',"000010000010000"},{'R',"110101110101101"},{'C',"111100100100111"},
    {'I',"111010010010111"},{'P',"111101111100100"},{'D',"110101101101110"},
    {'N',"111101101101101"},{'S',"111100111001111"},{'H',"101101111101101"},
    {'T',"111010010010010"},{'M',"101111111101101"},{'L',"100100100100111"},
    {'E',"111100110100111"},{'G',"111100101101111"},{'F',"111100110100100"},
};

static void draw_text(SDL_Renderer *ren, int x, int y, int s, const char *t)
{
    for (; *t; t++, x += 4 * s) {
        for (size_t g = 0; g < sizeof glyphs / sizeof glyphs[0]; g++) {
            if (glyphs[g].c != *t) continue;
            for (int i = 0; i < 15; i++) {
                if (glyphs[g].rows[i] == '1') {
                    SDL_Rect r = {x + (i % 3) * s, y + (i / 3) * s, s, s};
                    SDL_RenderFillRect(ren, &r);
                }
            }
        }
    }
}

static void ip_to_str(const ip4_addr_t *a, char *out, size_t n)
{
    uint32_t v = ip4_addr_get_u32(a);
    snprintf(out, n, "%u.%u.%u.%u", (unsigned)(v & 255), (unsigned)((v >> 8) & 255),
             (unsigned)((v >> 16) & 255), (unsigned)((v >> 24) & 255));
}

// Soft reset para o dashboard. Se existir D:\\DASH.TXT (uma linha com o caminho do XBE do
// dashboard, ex.: C:\\xboxdash.xbe) lanca esse XBE; senao pede o dashboard ao kernel.
// Teste de rede em segundo plano: HTTP e HTTPS para example.com
typedef struct { volatile int done, status, stage, err, len, p; } net_test_t;
static net_test_t g_t_http, g_t_https;
static volatile int g_nettest_started;

static void run_one(net_test_t *t, const char *url)
{
    ns_http_resp r;
    int rc = ns_http_get(url, &r);
    t->status = r.status; t->stage = rc ? r.stage : 0; t->err = rc ? r.err : 0;
    t->len = (int)r.len;
    t->p = (rc == 0 && r.data) ? nstest_count_tag((const char *)r.data, r.len, "p") : -1;
    ns_http_free(&r);
    t->done = 1;
}

static DWORD WINAPI net_test_thread(LPVOID arg)
{
    (void)arg;
    run_one(&g_t_http, "http://example.com/");
    run_one(&g_t_https, "https://example.com/");
    return 0;
}

static void draw_net_test(SDL_Renderer *ren, int y, const char *label, const net_test_t *t)
{
    char line[56];
    if (!t->done) snprintf(line, sizeof line, "%s ...", label);
    else if (t->stage) snprintf(line, sizeof line, "%s ST %d E %d", label, t->stage, t->err);
    else snprintf(line, sizeof line, "%s %d L %d P %d", label, t->status, t->len, t->p);
    draw_text(ren, 16, y, 4, line);
}

static SDL_Window *g_win;
static SDL_Renderer *g_ren;

static void return_to_dashboard(void)
{
    char path[200] = "";
    FILE *f = fopen("D:\\DASH.TXT", "r");
    if (f) {
        if (fgets(path, sizeof path, f)) {
            size_t n = strlen(path);
            while (n && (path[n - 1] == '\n' || path[n - 1] == '\r' || path[n - 1] == ' ')) path[--n] = 0;
        }
        fclose(f);
    }
    // desliga o video do SDL antes do reset (evita ecra a meio e a app presa)
    if (g_ren) { SDL_DestroyRenderer(g_ren); g_ren = NULL; }
    if (g_win) { SDL_DestroyWindow(g_win); g_win = NULL; }
    SDL_Quit();
    Sleep(50);
    // se o pedido voltar sem reiniciar, repete-o (antes era preciso carregar 2x)
    for (;;) {
        XLaunchXBE(path[0] ? path : NULL);
        Sleep(200);
    }
}

int main(void)
{
    XVideoSetMode(W, H, 32, REFRESH_DEFAULT);

    // Le a rede configurada no dashboard (IP auto/manual, DNS auto/manual) e aplica-a explicitamente.
    // (equivale ao modo AUTO do nxdk, mas explicito, e mostra no ecra o que foi lido)
    nxdk_network_config_sector_t cfg;
    memset(&cfg, 0, sizeof cfg);
    bool cfg_ok = nxLoadNetworkConfig(&cfg);
    uint32_t cfg_flags = cfg_ok ? cfg.dhcpFlags : 0;

    nx_net_parameters_t np;
    memset(&np, 0, sizeof np);
    np.ipv4_mode = NX_NET_DHCP;
    np.ipv6_mode = NX_NET_DHCP;
    if (cfg_flags & NXDK_NETWORK_CONFIG_MANUAL_IP) {
        np.ipv4_mode = NX_NET_STATIC;
        np.ipv4_ip = cfg.manual.ip;
        np.ipv4_netmask = cfg.manual.subnetMask;
        np.ipv4_gateway = cfg.manual.defaultGateway;
    }
    if (cfg_flags & NXDK_NETWORK_CONFIG_MANUAL_DNS) {   // ex.: DNS do Insignia
        np.ipv4_dns1 = cfg.manual.primaryDns;
        np.ipv4_dns2 = cfg.manual.secondaryDns;
    }
    int net_rc = nxNetInit(&np);   // 0 ok, -1 config/netif, -2 DHCP sem resposta (continua a tentar)

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        debugPrint("SDL_Init falhou: %s\n", SDL_GetError());
        Sleep(5000);
        return 1;
    }
    SDL_Window *win = SDL_CreateWindow("NetSurfX", 0, 0, W, H, SDL_WINDOW_SHOWN);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    g_win = win; g_ren = ren;

    int parsed = nstest_parse(); // esperado: 2
    SDL_GameController *pad = NULL;
    float cx = W / 2, cy = H / 2;
    bool running = true;
    bool dns_fixed = false;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_CONTROLLERDEVICEADDED && !pad)
                pad = SDL_GameControllerOpen(e.cdevice.which);
        }
        // IGR: L + R (gatilhos) + Back + Black -> volta ao dashboard, instantaneo (soft reset)
        if (pad &&
            SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000 &&
            SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000 &&
            SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_BACK) &&
            SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) { // Black
            return_to_dashboard();
        }
        if (pad) {
            float ax = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) / 32768.0f;
            float ay = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) / 32768.0f;
            if (ax > -0.15f && ax < 0.15f) ax = 0;
            if (ay > -0.15f && ay < 0.15f) ay = 0;
            cx += ax * 8; cy += ay * 8;
            if (cx < 0) cx = 0; if (cx > W - 1) cx = W - 1;
            if (cy < 0) cy = 0; if (cy > H - 1) cy = H - 1;
        }

        // estado da rede em direto (o DHCP pode demorar mais que o tempo de espera inicial)
        char ipstr[24] = "0.0.0.0", dnsstr[24] = "0.0.0.0";
        bool have_ip = false;
        if (netif_default && !ip4_addr_isany_val(*netif_ip4_addr(netif_default))) {
            have_ip = true;
            ip_to_str(netif_ip4_addr(netif_default), ipstr, sizeof ipstr);
            // O nxdk so aplica o DNS se o DHCP acabar a tempo (RC 0). Com RC -2 o IP chega
            // depois e o DNS manual do dashboard ficava por aplicar: aplicamo-lo aqui.
            if (!dns_fixed) {
                if (cfg_flags & NXDK_NETWORK_CONFIG_MANUAL_DNS) {
                    uint32_t v1 = cfg.manual.primaryDns, v2 = cfg.manual.secondaryDns;
                    ip_addr_t g;
                    if (v1) {
                        IP_ADDR4(&g, v1 & 0xff, (v1 >> 8) & 0xff, (v1 >> 16) & 0xff, (v1 >> 24) & 0xff);
                        dns_setserver(0, &g);
                    }
                    if (v2) {
                        IP_ADDR4(&g, v2 & 0xff, (v2 >> 8) & 0xff, (v2 >> 16) & 0xff, (v2 >> 24) & 0xff);
                        dns_setserver(1, &g);
                    }
                } else {
                    const ip_addr_t *cur = dns_getserver(0);
                    if (cur && ip4_addr_isany_val(*ip_2_ip4(cur))) {
                        ip_addr_t g; IP_ADDR4(&g, 1, 1, 1, 1);   // DNS de recurso
                        dns_setserver(0, &g);
                    }
                }
                dns_fixed = true;
            }
            if (!g_nettest_started) {
                g_nettest_started = 1;
                CreateThread(NULL, 256 * 1024, net_test_thread, NULL, 0, NULL);
            }
            const ip_addr_t *d;
            d = dns_getserver(0);
            if (d) ip_to_str(ip_2_ip4(d), dnsstr, sizeof dnsstr);
        }

        SDL_SetRenderDrawColor(ren, 245, 245, 245, 255);
        SDL_RenderClear(ren);

        // barra 1: rede (verde = tem IP, amarelo = a espera de DHCP, vermelho = erro)
        SDL_Rect bar = {0, 0, W, 24};
        if (have_ip) SDL_SetRenderDrawColor(ren, 40, 160, 60, 255);
        else if (net_rc == -2) SDL_SetRenderDrawColor(ren, 220, 190, 20, 255);
        else SDL_SetRenderDrawColor(ren, 190, 40, 40, 255);
        SDL_RenderFillRect(ren, &bar);

        // barra 2: azul = parse HTML ok
        SDL_Rect bar2 = {0, 24, W, 24};
        if (parsed == 2) SDL_SetRenderDrawColor(ren, 40, 90, 200, 255);
        else             SDL_SetRenderDrawColor(ren, 230, 140, 20, 255);
        SDL_RenderFillRect(ren, &bar2);

        // texto de diagnostico
        char line[48];
        SDL_SetRenderDrawColor(ren, 20, 20, 20, 255);
        snprintf(line, sizeof line, "NET RC %d", net_rc);
        draw_text(ren, 16, 70, 4, line);
        snprintf(line, sizeof line, "IP %s", ipstr);
        draw_text(ren, 16, 110, 4, line);
        snprintf(line, sizeof line, "DNS %s", dnsstr);
        draw_text(ren, 16, 150, 4, line);
        snprintf(line, sizeof line, "HTML P %d", parsed);
        draw_text(ren, 16, 190, 4, line);
        snprintf(line, sizeof line, "CFG %d FL %u", cfg_ok ? 1 : 0, (unsigned)cfg_flags);
        draw_text(ren, 16, 230, 4, line);
        draw_net_test(ren, 290, "HTTP", &g_t_http);
        draw_net_test(ren, 330, "HTTPS", &g_t_https);

        SDL_Rect cur = {(int)cx, (int)cy, 10, 10};
        SDL_RenderFillRect(ren, &cur);
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    return_to_dashboard();   // saida normal tambem volta ao dashboard
    return 0;
}

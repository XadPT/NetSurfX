import sys, re
src, out = sys.argv[1], sys.argv[2]
body = open(src).read().split('%%')[1]
ents = []
for line in body.strip().splitlines():
    line = line.strip()
    if not line: continue
    n, t = [x.strip() for x in line.split(',', 1)]
    ents.append((n.lower(), t))
ents.sort()
with open(out, 'w') as f:
    f.write('/* Replacement for gperf output: sorted table + binary search */\n')
    f.write('#include <string.h>\n\n')
    f.write('static const struct element_type_map wordlist[] = {\n')
    for n, t in ents:
        f.write('\t{ "%s", %s },\n' % (n, t))
    f.write('};\n\n')
    f.write('''static const struct element_type_map *hubbub_element_type_generated_lookup(
		const char *str, size_t len)
{
	size_t lo = 0, hi = sizeof(wordlist) /
			sizeof(wordlist[0]);
	char buf[32];
	size_t i;
	if (len == 0 || len >= sizeof(buf)) return NULL;
	for (i = 0; i < len; i++) {
		char c = str[i];
		buf[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
	}
	buf[len] = 0;
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		int c = strcmp(buf, wordlist[mid].name);
		if (c == 0) return &wordlist[mid];
		if (c < 0) hi = mid; else lo = mid + 1;
	}
	return NULL;
}
''')

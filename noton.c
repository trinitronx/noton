#include <SDL2/SDL.h>
#include <portmidi.h>
#include <porttime.h>
#include <stdio.h>

#define HOR 32
#define VER 16
#define PAD 8
#define ZOOM 2
#define color1 0xeeeeee
#define color2 0x000000
#define color3 0xcccccc
#define color4 0x72dec2
#define color0 0xffb545

#define GATEMAX 64
#define WIREMAX 128
#define WIREPTMAX 128
#define PORTMAX 32
#define INPUTMAX 8
#define OUTPUTMAX 12

#define DEVICE 0

typedef enum {
	INPUT,
	OUTPUT,
	XOR
} GateType;

typedef struct {
	int x, y;
} Point2d;

typedef struct Wire {
	int id, polarity, a, b, len;
	Point2d points[WIREPTMAX];
} Wire;

typedef struct Gate {
	int id, polarity, locked, inlen, outlen;
	int chan, note, shrp;
	Point2d pos;
	GateType type;
	Wire *inputs[PORTMAX], *outputs[PORTMAX];
} Gate;

typedef struct Noton {
	unsigned int alive, frame, speed, channel, octave;
	int glen, wlen;
	Gate gates[GATEMAX];
	Wire wires[WIREMAX];
	PmStream *midi;
	Gate *inputs[INPUTMAX], *outputs[OUTPUTMAX];
} Noton;

typedef struct Brush {
	int down;
	Point2d pos;
	Wire wire;
} Brush;

int WIDTH = 8 * HOR + PAD * 2;
int HEIGHT = 8 * VER + PAD * 2;
SDL_Window *gWindow = NULL;
SDL_Renderer *gRenderer = NULL;
SDL_Texture *gTexture = NULL;
uint32_t *pixels;
Noton noton;

/* generics */

int
distance(Point2d a, Point2d b)
{
	return (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y);
}

Point2d *
setpt2d(Point2d *p, int x, int y)
{
	p->x = x;
	p->y = y;
	return p;
}

Point2d *
cpypt2d(Point2d *dst, Point2d *src)
{
	dst->x = src->x;
	dst->y = src->y;
	return dst;
}

Point2d
Pt2d(int x, int y)
{
	Point2d pos;
	setpt2d(&pos, x, y);
	return pos;
}

int
polarcolor(int polarity)
{
	return polarity == 1 ? color4 : !polarity ? color0 : color3;
}

/* Midi */

void
initmidi(void)
{
	int i;
	Pm_Initialize();
	for(i = 0; i < Pm_CountDevices(); ++i) {
		char const *name = Pm_GetDeviceInfo(i)->name;
		printf("Device #%d -> %s%s\n", i, name, i == DEVICE ? "[x]" : "[ ]");
	}
	Pm_OpenOutput(&noton.midi, DEVICE, NULL, 128, 0, NULL, 1);
}

void
playmidi(int channel, int octave, int note, int z)
{
	Pm_WriteShort(noton.midi,
		Pt_Time(),
		Pm_Message(0x90 + channel, (octave * 12) + note, z ? 100 : 0));
}

/* Helpers */

Gate *
nearestgate(Noton *n, Point2d pos)
{
	int i;
	for(i = 0; i < n->glen; ++i) {
		Gate *g = &n->gates[i];
		if(distance(pos, g->pos) < 50)
			return g;
	}
	return NULL;
}

int
getpolarity(Gate *g)
{
	int i;
	if(g->inlen < 1)
		return -1;
	if(g->inlen == 1)
		return g->inputs[0]->polarity;
	for(i = 0; i < g->inlen; i++)
		if(g->inputs[i]->polarity != g->inputs[0]->polarity)
			return 0;
	return 1;
}

void
polarize(Gate *g)
{
	int i;
	if(g->type == OUTPUT) {
		int newpolarity = getpolarity(g);
		if(newpolarity != -1 && g->polarity != newpolarity)
			playmidi(noton.channel, noton.octave, g->note, newpolarity);
		g->polarity = newpolarity;
	} else if(g->type)
		g->polarity = getpolarity(g);
	for(i = 0; i < g->outlen; ++i)
		g->outputs[i]->polarity = g->polarity;
}

void
bang(Gate *g, int depth)
{
	int i, d = depth - 1;
	if(d && g) {
		polarize(g);
		for(i = 0; i < g->outlen; ++i)
			if(&noton.gates[g->outputs[i]->b])
				bang(&noton.gates[g->outputs[i]->b], d);
	}
}

/* Options */

void
select(Noton *n, int channel)
{
	n->channel = channel;
	printf("Select channel #%d\n", channel);
}

void
toggle(Noton *n)
{
	n->alive = !n->alive;
	printf("Toggle %s\n", n->alive ? "play" : "pause");
}

void
destroy(Noton *n)
{
	/* TODO */
}

/* Add/Remove */

Wire *
addwire(Noton *n, Wire *temp, Gate *from, Gate *to)
{
	int i;
	Wire *w = &n->wires[n->wlen];
	w->id = n->wlen++;
	w->polarity = -1;
	w->a = from->id;
	w->b = to->id;
	w->len = 0;
	for(i = 0; i < temp->len; i++)
		setpt2d(&w->points[w->len++], temp->points[i].x, temp->points[i].y);
	printf("Add wire #%d(#%d->#%d) \n", w->id, from->id, to->id);
	return w;
}

Gate *
addgate(Noton *n, GateType type, int polarity, Point2d pos)
{
	Gate *g = &n->gates[n->glen];
	g->id = n->glen++;
	g->polarity = polarity;
	g->chan = 0;
	g->note = 0;
	g->shrp = 0;
	g->inlen = 0;
	g->outlen = 0;
	g->type = type;
	g->pos = pos;
	printf("Add gate #%d \n", g->id);
	return g;
}

/* Wiring */

void
extendwire(Brush *b)
{
	if(b->wire.len >= WIREPTMAX)
		return;
	if(distance(b->wire.points[b->wire.len - 1], b->pos) < 20)
		return;
	cpypt2d(&b->wire.points[b->wire.len++], &b->pos);
}

void
beginwire(Brush *b)
{
	Gate *gate = nearestgate(&noton, b->pos);
	b->wire.polarity = gate ? gate->polarity : -1;
	b->wire.len = 0;
	cpypt2d(&b->wire.points[b->wire.len++], gate ? &gate->pos : &b->pos);
}

int
abandon(Brush *b)
{
	b->wire.len = 0;
	return 0;
}

int
endwire(Brush *b)
{
	Wire *newwire;
	Gate *gatefrom, *gateto;
	if(b->wire.len < 1)
		return abandon(b);
	gatefrom = nearestgate(&noton, b->wire.points[0]);
	if(!gatefrom)
		return abandon(b);
	gateto = nearestgate(&noton, b->pos);
	if(!gateto)
		return abandon(b);
	if(!gateto->type)
		return abandon(b);
	if(gatefrom == gateto)
		return abandon(b);
	if(gatefrom->outlen >= PORTMAX)
		return abandon(b);
	if(gateto->inlen >= PORTMAX)
		return abandon(b);
	setpt2d(&b->pos, gateto->pos.x, gateto->pos.y);
	extendwire(b);
	/* copy */
	newwire = addwire(&noton, &b->wire, gatefrom, gateto);
	/* connect */
	gatefrom->outputs[gatefrom->outlen++] = newwire;
	gateto->inputs[gateto->inlen++] = newwire;
	polarize(gateto);
	return abandon(b);
}

/* draw */

void
pixel(uint32_t *dst, int x, int y, int color)
{
	if(x >= 0 && x < WIDTH - PAD * 2 && y >= 0 && y < HEIGHT - PAD * 2)
		dst[(y + PAD) * WIDTH + (x + PAD)] = color;
}

void
line(uint32_t *dst, int ax, int ay, int bx, int by, int color)
{
	int dx = abs(bx - ax), sx = ax < bx ? 1 : -1;
	int dy = -abs(by - ay), sy = ay < by ? 1 : -1;
	int err = dx + dy, e2;
	for(;;) {
		pixel(dst, ax, ay, color);
		if(ax == bx && ay == by)
			break;
		e2 = 2 * err;
		if(e2 >= dy) {
			err += dy;
			ax += sx;
		}
		if(e2 <= dx) {
			err += dx;
			ay += sy;
		}
	}
}

void
circle(uint32_t *dst, int ax, int ay, int d, int color)
{
	int i, r = d / 2;
	for(i = 0; i < d * d; ++i) {
		int x = i % d, y = i / d;
		if(distance(Pt2d(ax, ay), Pt2d(ax - r + x, ay - r + y)) < d)
			pixel(dst, ax - r + x, ay - r + y, color);
	}
}

void
drawwire(uint32_t *dst, Wire *w, int color)
{
	int i;
	for(i = 0; i < w->len - 1; i++) {
		Point2d p1 = w->points[i];
		Point2d *p2 = &w->points[i + 1];
		if(p2) {
			line(dst, p1.x, p1.y, p2->x, p2->y, color);
			if((int)(noton.frame / 3) % w->len != i)
				line(dst, p1.x, p1.y, p2->x, p2->y, polarcolor(w->polarity));
		}
	}
}

void
drawgate(uint32_t *dst, Gate *g)
{
	int r = 17;
	circle(dst, g->pos.x, g->pos.y, r, polarcolor(g->polarity));
	if(g->type == OUTPUT) {
		pixel(dst, g->pos.x - 1, g->pos.y, g->shrp ? color2 : color1);
		pixel(dst, g->pos.x + 1, g->pos.y, g->shrp ? color2 : color1);
		pixel(dst, g->pos.x, g->pos.y - 1, g->shrp ? color2 : color1);
		pixel(dst, g->pos.x, g->pos.y + 1, g->shrp ? color2 : color1);
	} else
		pixel(dst, g->pos.x, g->pos.y, color1);
}

void
clear(uint32_t *dst)
{
	int i, j;
	for(i = 0; i < HEIGHT; i++)
		for(j = 0; j < WIDTH; j++)
			dst[i * WIDTH + j] = color1;
}

void
redraw(uint32_t *dst, Brush *b)
{
	int i;
	clear(dst);
	for(i = 0; i < noton.glen; i++)
		drawgate(dst, &noton.gates[i]);
	for(i = 0; i < noton.wlen; i++)
		drawwire(dst, &noton.wires[i], color2);
	drawwire(dst, &b->wire, color3);
	SDL_UpdateTexture(gTexture, NULL, dst, WIDTH * sizeof(uint32_t));
	SDL_RenderClear(gRenderer);
	SDL_RenderCopy(gRenderer, gTexture, NULL, NULL);
	SDL_RenderPresent(gRenderer);
}

/* operation */

void
run(Noton *n)
{
	int i;
	n->inputs[0]->polarity = (n->frame / 4) % 2;
	n->inputs[2]->polarity = (n->frame / 8) % 2;
	n->inputs[4]->polarity = (n->frame / 16) % 2;
	n->inputs[6]->polarity = (n->frame / 32) % 2;
	n->inputs[1]->polarity = (n->frame / 8) % 4 == 0;
	n->inputs[3]->polarity = (n->frame / 8) % 4 == 1;
	n->inputs[5]->polarity = (n->frame / 8) % 4 == 2;
	n->inputs[7]->polarity = (n->frame / 8) % 4 == 3;
	for(i = 0; i < n->glen; ++i)
		bang(&n->gates[i], 10);
	n->frame++;
}

void
setup(Noton *n)
{
	int i;
	Gate *gtrue, *gfalse;
	int octave[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
	for(i = 0; i < INPUTMAX; ++i) {
		int x = i % 2 == 0 ? 27 : 20;
		n->inputs[i] = addgate(n, INPUT, 0, Pt2d(x, 30 + i * 6));
		n->inputs[i]->locked = 1;
	}
	for(i = 0; i < OUTPUTMAX; ++i) {
		int x = WIDTH - (i % 2 == 0 ? 61 : 54);
		n->outputs[i] = addgate(n, OUTPUT, 0, Pt2d(x, 30 + i * 6));
		n->outputs[i]->locked = 1;
		n->outputs[i]->note = i;
		n->outputs[i]->chan = 0;
		n->outputs[i]->shrp = octave[abs(n->outputs[i]->note) % 12];
	}
	for(i = 0; i < OUTPUTMAX; ++i) {
		int x = WIDTH - (i % 2 == 0 ? 47 : 40);
		n->outputs[i] = addgate(n, OUTPUT, 0, Pt2d(x, 30 + i * 6));
		n->outputs[i]->locked = 1;
		n->outputs[i]->note = i + 24;
		n->outputs[i]->chan = 0;
		n->outputs[i]->shrp = octave[abs(n->outputs[i]->note) % 12];
	}
	gfalse = addgate(n, INPUT, 0, Pt2d((10 % 2 == 0 ? 26 : 20), 30 + 10 * 6));
	gfalse->locked = 1;
	gtrue = addgate(n, INPUT, 1, Pt2d((11 % 2 == 0 ? 26 : 20), 30 + 11 * 6));
	gtrue->locked = 1;
}

/* options */

int
error(char *msg, const char *err)
{
	printf("Error %s: %s\n", msg, err);
	return 0;
}

void
quit(void)
{
	free(pixels);
	Pm_Terminate();
	SDL_DestroyTexture(gTexture);
	gTexture = NULL;
	SDL_DestroyRenderer(gRenderer);
	gRenderer = NULL;
	SDL_DestroyWindow(gWindow);
	gWindow = NULL;
	SDL_Quit();
	exit(0);
}

void
domouse(SDL_Event *event, Brush *b)
{
	switch(event->type) {
	case SDL_MOUSEBUTTONDOWN:
		setpt2d(&b->pos,
			(event->motion.x - (PAD * ZOOM)) / ZOOM,
			(event->motion.y - (PAD * ZOOM)) / ZOOM);
		if(event->button.button == SDL_BUTTON_RIGHT)
			break;
		b->down = 1;
		beginwire(b);
		redraw(pixels, b);
		break;
	case SDL_MOUSEMOTION:
		if(event->button.button == SDL_BUTTON_RIGHT)
			break;
		if(b->down) {
			setpt2d(&b->pos,
				(event->motion.x - (PAD * ZOOM)) / ZOOM,
				(event->motion.y - (PAD * ZOOM)) / ZOOM);
			extendwire(b);
			redraw(pixels, b);
		}
		break;
	case SDL_MOUSEBUTTONUP:
		setpt2d(&b->pos,
			(event->motion.x - (PAD * ZOOM)) / ZOOM,
			(event->motion.y - (PAD * ZOOM)) / ZOOM);
		if(event->button.button == SDL_BUTTON_RIGHT) {
			if(!nearestgate(&noton, b->pos))
				addgate(&noton, XOR, -1, b->pos);
			redraw(pixels, b);
			break;
		}
		b->down = 0;
		endwire(b);
		redraw(pixels, b);
		break;
	}
}

void
dokey(Noton *n, SDL_Event *event, Brush *b)
{
	/* int shift = SDL_GetModState() & KMOD_LSHIFT || SDL_GetModState() & KMOD_RSHIFT; */
	switch(event->key.keysym.sym) {
	case SDLK_ESCAPE: quit(); break;
	case SDLK_BACKSPACE: redraw(pixels, b); break;
	case SDLK_SPACE: toggle(n); break;
	case SDLK_n: destroy(n); break;
	case SDLK_1: select(n, 0); break;
	case SDLK_2: select(n, 1); break;
	case SDLK_3: select(n, 2); break;
	case SDLK_4: select(n, 3); break;
	case SDLK_5: select(n, 4); break;
	case SDLK_6: select(n, 5); break;
	case SDLK_7: select(n, 6); break;
	case SDLK_8: select(n, 7); break;
	case SDLK_9: select(n, 8); break;
	}
}

int
init(void)
{
	if(SDL_Init(SDL_INIT_VIDEO) < 0)
		return error("Init", SDL_GetError());
	printf("init\n");
	gWindow = SDL_CreateWindow("Noton",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		WIDTH * ZOOM,
		HEIGHT * ZOOM,
		SDL_WINDOW_SHOWN);
	if(gWindow == NULL)
		return error("Window", SDL_GetError());
	gRenderer = SDL_CreateRenderer(gWindow, -1, 0);
	if(gRenderer == NULL)
		return error("Renderer", SDL_GetError());
	gTexture = SDL_CreateTexture(gRenderer,
		SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STATIC,
		WIDTH,
		HEIGHT);
	if(gTexture == NULL)
		return error("Texture", SDL_GetError());
	pixels = (uint32_t *)malloc(WIDTH * HEIGHT * sizeof(uint32_t));
	if(pixels == NULL)
		return error("Pixels", "Failed to allocate memory");
	clear(pixels);
	initmidi();
	return 1;
}

int
main(int argc, char **argv)
{
	Uint32 begintime = 0;
	Uint32 endtime = 0;
	Uint32 delta = 0;
	short fps = 60;

	Brush brush;
	brush.down = 0;
	brush.wire.len = 0;

	noton.alive = 1;
	noton.speed = 40;
	noton.channel = 0;
	noton.octave = 3;

	if(!init())
		return error("Init", "Failure");

	setup(&noton);

	while(1) {
		SDL_Event event;
		if(!begintime)
			begintime = SDL_GetTicks();
		else
			delta = endtime - begintime;

		if(delta < noton.speed)
			SDL_Delay(noton.speed - delta);
		if(delta > noton.speed)
			fps = 1000 / delta;
		if(fps < 15)
			printf("Slowdown: %ifps\n", fps);

		if(noton.alive) {
			run(&noton);
			redraw(pixels, &brush);
		}

		while(SDL_PollEvent(&event) != 0) {
			if(event.type == SDL_QUIT)
				quit();
			else if(event.type == SDL_MOUSEBUTTONUP ||
					event.type == SDL_MOUSEBUTTONDOWN ||
					event.type == SDL_MOUSEMOTION) {
				domouse(&event, &brush);
			} else if(event.type == SDL_KEYDOWN)
				dokey(&noton, &event, &brush);
			else if(event.type == SDL_WINDOWEVENT)
				if(event.window.event == SDL_WINDOWEVENT_EXPOSED)
					redraw(pixels, &brush);
		}

		begintime = endtime;
		endtime = SDL_GetTicks();
	}
	quit();
	(void)argc;
	(void)argv;
	return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <windows.h>

// 2 channels
// 8000 bytes per second
static struct {
     HWAVEOUT hWaveOut;
     HANDLE audioThread;
     WAVEFORMATEX waveFormat;
     int index;
     int musicIndex;
     short buffer[24000]; // three seconds of sound
     bool musicPlaying;
     bool paused, locked;
} Audio;

#define BUFFERLEN (sizeof(Audio.buffer)/sizeof(*Audio.buffer))
#define PARTCNT (sizeof(Audio.parts)/sizeof(*Audio.parts))
#define PARTSIZE (sizeof(Audio.buffer)/PARTCNT)

#define EFFECT_ROTATE 1
#define EFFECT_LINECLEAR 2
#define EFFECT_TETRIS 3
#define EFFECT_GAMEOVER 4
#define EFFECT_FELL 5
#define EFFECT_MAX 5
struct {
    short *data;
    int dataLength;
} AudioBuffers[EFFECT_MAX + 1];

#define WM_SETACTIVEWINDOW 0xFF0F
#define WM_START 0xFFF0
HWND MainWindow;

HBRUSH WallBrush;
int OffsetX, OffsetY;
int TileWidth = 20, TileHeight = 25;
HDC BufferDc;
HDC SpriteDc;
int BufferWidth, BufferHeight;

#define GRID_WIDTH 10
#define GRID_HEIGHT 18
#define GRID_SIZE (GRID_WIDTH*GRID_HEIGHT)
char Grid[GRID_SIZE];

struct Piece {
    char gs;
    char *grid;
} Pieces[7];

HDC FontDc;

void CALLBACK WaveOutputCallback(HWAVEOUT hwo, UINT uMsg, DWORD dwInstance, DWORD dwParam1, DWORD dwParam2)
{}

void PlaySoundEffect(int i)
{
    int ind, len;
    short *src;

    ind = Audio.index;
    len = AudioBuffers[i].dataLength;
    src = AudioBuffers[i].data;
    while(len)
    {
        *(short*) ((void*) Audio.buffer + ind) += *src;
        ind += 2;
        ind %= sizeof(Audio.buffer);
        src++;
        len -= 2;
    }
}

void AudioStartMusic(void)
{
    memcpy(Audio.buffer, AudioBuffers[0].data, sizeof(Audio.buffer));
    Audio.musicPlaying = 1;
    Audio.paused = 0;
    Audio.musicIndex = sizeof(Audio.buffer);
    Audio.index = 0;
}

void AudioStopMusic(void)
{
    Audio.musicPlaying = 0;
    Audio.musicIndex = 0;
    memset(Audio.buffer, 0, sizeof(Audio.buffer));
}

DWORD WINAPI AudioThread(void *unused)
{
    #define HDRSIZE 400
    static WAVEHDR hdr[4];
    int indexes[sizeof(hdr) / sizeof(*hdr)];
    int i;
    for(i = 0; i < sizeof(hdr) / sizeof(*hdr); i++)
    {
        waveOutPrepareHeader(Audio.hWaveOut, hdr + i, sizeof(*hdr));
        hdr[i].dwFlags |= WHDR_DONE;
        hdr[i].lpData = malloc(HDRSIZE);
        hdr[i].dwBufferLength = HDRSIZE;
    }
    while(1)
    {
        next:
        while(Audio.paused || Audio.locked)
            Sleep(10);
        for(i = 0; i < sizeof(hdr) / sizeof(*hdr); i++)
        {
            if(hdr[i].dwFlags & WHDR_DONE)
            {
                indexes[i] = Audio.index;
                memcpy(hdr[i].lpData, (void*) Audio.buffer + Audio.index, HDRSIZE);
                Audio.index += HDRSIZE;
                Audio.index %= sizeof(Audio.buffer);
                waveOutWrite(Audio.hWaveOut, hdr + i, sizeof(*hdr));
            }
            else
                indexes[i] = -1;
        }
        for(i = 0; i < sizeof(hdr) / sizeof(*hdr); i++)
        {
            if(indexes[i] >= 0)
            {
                if(Audio.musicPlaying) // replace written audio with music
                {
                    memcpy((void*) Audio.buffer + indexes[i], (void*) AudioBuffers[0].data + Audio.musicIndex, HDRSIZE);
                    Audio.musicIndex += HDRSIZE;
                    Audio.musicIndex %= AudioBuffers[0].dataLength;
                }
                else // replace written audio with silence
                {
                    memset((void*) Audio.buffer + indexes[i], 0, HDRSIZE);
                }
            }
        }
        while(1)
        {
            Sleep(1);
            for(i = 0; i < sizeof(hdr) / sizeof(*hdr); i++)
                if(hdr[i].dwFlags & WHDR_DONE)
                    goto next;
        }
    }
    return 0;
    #undef HDRSIZE
}

int CDrawText(HDC hdc, int scale, int x, int y, const char *text, int count)
{
    int i;
    UINT align;
    char ch;
    scale *= 8;
    align = GetTextAlign(hdc);
    if(align == TA_CENTER)
        x -= count * scale / 2;
    else if(align == TA_RIGHT)
        x -= count * scale;
    while(count--)
    {
        ch = *(text++);
        i = toupper(ch) - '!';
        i *= 8;
        StretchBlt(hdc, x, y, scale, 2 * scale, FontDc, i, 0, 8, 16, SRCPAINT);
        x += scale;
    }
    return x;
}

int CDrawCenteredText(HDC hdc, int scale, RECT *r, const char *text, int count)
{
    int x, y;
    x = (r->left + r->right) / 2;
    y = (r->top - scale * 16 + r->bottom) / 2;
    return CDrawText(hdc, scale, x, y, text, count);
}

LRESULT CALLBACK MainProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static HWND windows[2];
    static HWND activeWindow;
    RECT r;
    HBITMAP hBmp;
    HDC hdc;
    switch(msg)
    {
    case WM_CREATE:
        MainWindow = hWnd; // make it globally accessible
        hBmp = LoadImage(NULL, "wall.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
        WallBrush = CreatePatternBrush(hBmp);
        DeleteObject(hBmp);
        hdc = GetDC(hWnd);
        hBmp = LoadImage(NULL, "sprites.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
        SpriteDc = CreateCompatibleDC(hdc);
        SelectObject(SpriteDc, hBmp);
        hBmp = LoadImage(NULL, "font.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
        FontDc = CreateCompatibleDC(hdc);
        SelectObject(FontDc, hBmp);

        BufferDc = CreateCompatibleDC(hdc);
        hBmp = CreateCompatibleBitmap(hdc, BufferWidth = 500, BufferHeight = 660);
        SelectObject(BufferDc, hBmp);
        ReleaseDC(hWnd, hdc);
        OffsetX = (BufferWidth - GRID_WIDTH * TileWidth) / 2;
        OffsetY = (BufferHeight - GRID_HEIGHT * TileHeight) / 2;
        SetTextAlign(BufferDc, TA_CENTER);
        SetBkMode(BufferDc, TRANSPARENT);
        SetTextColor(BufferDc, 0xFFFFFF);

        windows[0] = CreateWindow("Game", NULL, WS_CHILD, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
        windows[1] = CreateWindow("GameMenu", NULL, WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
        activeWindow = windows[1];
        break;
    case WM_CLOSE:
        DestroyWindow(windows[0]);
        DestroyWindow(windows[1]);
        break;
    case WM_DESTROY:
        DeleteDC(BufferDc);
        DeleteDC(SpriteDc);
        DeleteObject(WallBrush);
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        MoveWindow(activeWindow, 0, 0, LOWORD(lParam), HIWORD(lParam), 0);
        break;
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_CHAR:
        SendMessage(activeWindow, msg, wParam, lParam);
        break;
    case WM_SETACTIVEWINDOW:
        activeWindow = windows[wParam];
        ShowWindow(windows[!wParam], SW_HIDE);
        GetClientRect(hWnd, &r);
        SendMessage(windows[wParam], WM_START, lParam, 0);
        SetWindowPos(windows[wParam], NULL, 0, 0, r.right, r.bottom, SWP_SHOWWINDOW);
        break;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK MenuProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static int selectedLevel;
    static int framePos;
    static int highscores[3 * 10];
    static char highscoreNames[3 * 10][12];
    static bool typing;
    static int typingInIndex;
    static int typingIndex;
    FILE *fp; // load/store save data
    HDC hdc;
    PAINTSTRUCT ps;
    RECT r;
    int i;
    char ch;
    char numBuf[20];
    switch(msg)
    {
    case WM_CREATE:
        SetTimer(hWnd, 0, 80, NULL);
        fp = fopen("Tetris.save", "rb");
        if(fp)
        {
            fread(highscores, 4, 30, fp);
            fread(highscoreNames, 12, 30, fp);
            fclose(fp);
        }
        else
            memset(highscoreNames, '.', sizeof(highscoreNames));
        return 0;
    case WM_DESTROY:
        fp = fopen("Tetris.save", "wb");
        fwrite(highscores, 4, 30, fp);
        fwrite(highscoreNames, 12, 30, fp);
        fclose(fp);
        break;
    case WM_TIMER:
        framePos = 16 - framePos;
        RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE);
        return 0;
    case WM_SIZE:
        RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE);
        return 0;
    case WM_START:
        for(i = 0; i < 3; i++)
        {
            if(wParam > highscores[selectedLevel * 3 + i])
            {
                memmove(highscores + selectedLevel * 3 + i + 1, highscores + selectedLevel * 3 + i, (2 - i) * 4);
                memmove(highscoreNames + selectedLevel * 3 + i + 1, highscoreNames + selectedLevel * 3 + i, (2 - i) * 12);
                highscores[selectedLevel * 3 + i] = wParam;
                typingInIndex = selectedLevel * 3 + i;
                typingIndex = 0;
                typing = 1;
                break;
            }
        }
        RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE);
        return 0;
    case WM_PAINT:
        hdc = BeginPaint(hWnd, &ps);
        SetTextAlign(BufferDc, TA_CENTER);
        r = (RECT) { 0, 0, BufferWidth, BufferHeight };
        FillRect(BufferDc, &r, WallBrush);
        r.left = BufferWidth / 2 - 100;
        r.right = r.left + 200;
        r.bottom = BufferHeight / 2 - 180;
        r.top = r.bottom - 100;
        CDrawCenteredText(BufferDc, 3, &r, "CHOOSE LEVEL", sizeof("CHOOSE LEVEL") - 1);
        for(i = 0; i <= 9; i++)
        {
            ch = i + '0';
            r = (RECT) { BufferWidth / 2 - 100 + (i % 5) * (200 / 5),
                         BufferHeight / 2 - 180 + (i / 5) * (160 / 2) };
            r.right = r.left + 200 / 5;
            r.bottom = r.top + 160 / 2;
            FillRect(BufferDc, &r, GetStockObject(DKGRAY_BRUSH));
            CDrawCenteredText(BufferDc, 3, &r, &ch, 1);
            if(!typing && i == selectedLevel)
            {
                StretchBlt(BufferDc, r.left, r.top, 200 / 5, 160 / 2, SpriteDc, framePos, 16, 16, 16, SRCPAINT);
            }
        }
        r = (RECT) { BufferWidth / 2 - 170, BufferHeight / 2 };
        r.right = r.left + 340;
        r.bottom = r.top + 60;
        CDrawCenteredText(BufferDc, 3, &r, "Highscores", sizeof("Highscores") - 1);
        r.top += 74;
        r.bottom += 74;
        for(i = 0; i < 3; i++)
        {
            FillRect(BufferDc, &r, GetStockObject(DKGRAY_BRUSH));
            if(highscores[i + selectedLevel * 3])
            {
                SetTextAlign(BufferDc, 0);
                numBuf[0] = i + '1';
                numBuf[1] = '.';
                CDrawText(BufferDc, 2, r.left, (r.top + r.bottom) / 2 - 16, numBuf, 2);
                CDrawText(BufferDc, 2, r.left + 37, (r.top + r.bottom) / 2 - 16, highscoreNames[i + selectedLevel * 3], 12);
                SetTextAlign(BufferDc, TA_RIGHT);
                sprintf(numBuf, "%d", highscores[i + selectedLevel * 3]);
                CDrawText(BufferDc, 2, r.right, (r.top + r.bottom) / 2 - 16, numBuf, strlen(numBuf));
            }
            else
            {
                SetTextAlign(BufferDc, 0);
                numBuf[0] = i + '1';
                numBuf[1] = '.';
                CDrawText(BufferDc, 2, r.left, (r.top + r.bottom) / 2 - 16, numBuf, 2);
                memset(numBuf, '.', 12);
                CDrawText(BufferDc, 2, r.left + 37, (r.top + r.bottom) / 2 - 16, numBuf, 12);
                SetTextAlign(BufferDc, TA_RIGHT);
                numBuf[0] = '0';
                CDrawText(BufferDc, 2, r.right, (r.top + r.bottom) / 2 - 16, numBuf, 1);
            }
            r.top += 60;
            r.bottom += 60;
        }

        if(typing)
        {
            StretchBlt(BufferDc, r.left + 37 + typingIndex * 16, (r.top + r.bottom) / 2 - 196 + 60 * (typingInIndex % 3), 16, 32, SpriteDc, framePos, 16, 16, 16, SRCPAINT);
        }

        GetClientRect(hWnd, &r);
        StretchBlt(hdc, 0, 0, r.right, r.bottom, BufferDc, 0, 0, BufferWidth, BufferHeight, SRCCOPY);
        EndPaint(hWnd, &ps);
        return 0;
    case WM_CHAR:
        if(typing)
        {
            wParam = toupper(wParam);
            if(wParam >= '!' && wParam <= 'Z')
            {
                highscoreNames[typingInIndex][typingIndex] = wParam;
                if(typingIndex != 11)
                    typingIndex++;
                RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE);
            }
        }
        return 0;
    case WM_KEYDOWN:
        if(typing)
        {
            switch(wParam)
            {
            case VK_LEFT: if(typingIndex) typingIndex--; else return 0; break;
            case VK_RIGHT: if(typingIndex != 11) typingIndex++; else return 0; break;
            case VK_BACK:
                highscoreNames[typingInIndex][typingIndex] = '.';
                if(typingIndex)
                    typingIndex--;
                break;
            case VK_DELETE: highscoreNames[typingInIndex][typingIndex] = '.'; break;
            case VK_RETURN: typing = 0; break;
            }
        }
        else switch(wParam)
        {
        case 'A': case VK_LEFT: if(selectedLevel) selectedLevel--; else return 0; break;
        case 'D': case VK_RIGHT: if(selectedLevel != 9) selectedLevel++; else return 0; break;
        case 'W': case VK_UP: if(selectedLevel >= 5) selectedLevel -= 5; else return 0; break;
        case 'S': case VK_DOWN: if(selectedLevel < 5) selectedLevel += 5; else return 0; break;
        case VK_RETURN: SendMessage(MainWindow, WM_SETACTIVEWINDOW, 0, selectedLevel);
        default:
            return 0;
        }
        RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE);
        return 0;
    case WM_LBUTTONDOWN:
        break;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

void RotateGrid(char *gr, char gs, char *dest, bool ccw)
{
    char x, y;
    for(y = 0; y < gs; y++)
    for(x = 0; x < gs; x++, gr++)
        if(ccw)
            dest[y + (gs - 1 - x) * gs] = *gr;
        else
            dest[(gs - 1 - y) + x * gs] = *gr;
}

bool CheckCollision(char *gr, char gs, int x, int y)
{
    char *dgr = Grid + x + y * GRID_WIDTH;
    char dx, dy;
    for(dy = 0; dy < gs; dy++, dgr += GRID_WIDTH - gs)
    for(dx = 0; dx < gs; dx++, dgr++, gr++)
    {
        if(!*gr)
            continue;
        if(x + dx < 0 || x + dx >= GRID_WIDTH || y + dy >= GRID_HEIGHT)
            return 0;
        if(*dgr)
            return 0;
    }
    return 1;
}

static char PieceGrids[] = {
      // O
      2, 1, 1,
         1, 1,
      // T
      3, 0, 2, 0,
         2, 2, 2,
         0, 0, 0,
      // S
      3, 0, 3, 3,
         3, 3, 0,
         0, 0, 0,
      // Z
      3, 4, 4, 0,
         0, 4, 4,
         0, 0, 0,
      // I
      4, 0, 0, 0, 0,
         5, 5, 5, 5,
         0, 0, 0, 0,
         0, 0, 0, 0,
      // J
      3, 6, 0, 0,
         6, 6, 6,
         0, 0, 0,
      // L
      3, 0, 0, 7,
         7, 7, 7,
         0, 0, 0,
};

void InitPieces(void)
{
    char *pg = PieceGrids;
    for(int i = 0; i < sizeof(Pieces) / sizeof(*Pieces); i++)
    {
        Pieces[i].gs = *pg;
        Pieces[i].grid = pg + 1;
        pg += *pg * *pg + 1;
    }
}

LRESULT CALLBACK GameProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // general game info
    static struct Piece *curPiece, *nextPiece;
    static int pieceX, pieceY;
    static char piece[16];
    static bool gameOver = 0;
    static bool paused = 0;
    static int score = 0;
    static int level = 0;
    static int color = 0xAA8000;
    static bool showColor = 1;
    static int startLevel = 0;
    static int lines = 0;
    static bool keys[255];
    // line clear animation
    static char lineIndexes[4];
    static int linesCleared;
    static int animationFrames = -1;
    static const int animationDuration = 12;
    // game over animation
    static int gameOverY;
    static int downTime = 7;

    char numBuf[20];
    char rot[16];
    HDC hdc;
    PAINTSTRUCT ps;
    char x, y;
    char *gr;
    RECT r;
    bool ccw;
    bool isFull;
    int height;
    int i;
    int dx;
    switch(msg)
    {
    case WM_CREATE:
        return 0;
    case WM_SIZE:
        RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE);
        break;
    case WM_START:
        curPiece = Pieces + rand() % (sizeof(Pieces) / sizeof(*Pieces));
        memcpy(piece, curPiece->grid, curPiece->gs * curPiece->gs);
        nextPiece = Pieces + rand() % (sizeof(Pieces) / sizeof(*Pieces));
        pieceX = GRID_WIDTH / 2 - 1;
        pieceY = 0;
        level = startLevel = wParam;
        SetTimer(hWnd, 0, level >= 14 ? 20 : 860 - level * 60, NULL);
        AudioStartMusic();
        break;
    case WM_TIMER:
        if(paused)
            return 0;
        if(wParam == 2) // line clear animation
        {
            if(!animationFrames)
            {
                switch(linesCleared)
                {
                case 1: score += 40 * (level + 1); break;
                case 2: score += 100 * (level + 1); break;
                case 3: score += 300 * (level + 1); break;
                case 4: score += 1200 * (level + 1); break;
                }
                if(lines % 10 > (lines + linesCleared) % 10)
                {
                    if(startLevel * 10 <= lines)
                    {
                        KillTimer(hWnd, 0);
                        SetTimer(hWnd, 0, level >= 25 ? 20 :
                                          level >= 20 ? 30 - (level - 20) * 2 :
                                          level >= 14 ? 80 - (level - 14) * 10 :
                                          860 - level * 60, NULL);
                        level++;
                    }
                }
                if(lines % GRID_HEIGHT > (lines + linesCleared) % GRID_HEIGHT)
                {
                    color >>= 8;
                    if(!color)
                    {
                        x = rand() & 0xFF;
                        color = (x << 16) | x;
                    }
                }
                lines += linesCleared;
                x = 0;
                i = linesCleared;
                while(i--)
                {
                    gr = Grid + GRID_WIDTH * (lineIndexes[i] + x);
                    for(y = lineIndexes[i] + x; y >= 1; y--)
                    {
                        memcpy(gr, gr - GRID_WIDTH, GRID_WIDTH);
                        gr -= GRID_WIDTH;
                    }
                    x++;
                }
                memset(Grid, 0, GRID_WIDTH * linesCleared);
                curPiece = nextPiece;
                memcpy(piece, curPiece->grid, curPiece->gs * curPiece->gs);
                nextPiece = Pieces + rand() % (sizeof(Pieces) / sizeof(*Pieces));
                KillTimer(hWnd, 2);
            }
            animationFrames--;
            RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE);
            return 0;
        }
        else if(wParam == 3)
        {
            downTime--;
            if(downTime > 0)
                return 0;
            if(gameOverY != GRID_HEIGHT)
            {
                memset(Grid + gameOverY * GRID_WIDTH, 0, GRID_WIDTH);
                gameOverY++;
            }
            else
                KillTimer(hWnd, 3);
            RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE);
            return 0;
        }
        if(animationFrames >= 0)
            return 0;
        if(!CheckCollision(piece, curPiece->gs, pieceX, pieceY + 1))
        {
            gr = piece;
            // putting piece into grid
            for(y = 0; y < curPiece->gs; y++)
            for(x = 0; x < curPiece->gs; x++, gr++)
                if(*gr)
                {
                    if(Grid[(pieceX + x) + (pieceY + y) * GRID_WIDTH])
                    {
                        gameOver = 1;
                        AudioStopMusic();
                        PlaySoundEffect(EFFECT_GAMEOVER);
                        KillTimer(hWnd, 0);
                        KillTimer(hWnd, 1);
                        SetTimer(hWnd, 3, 40, NULL); // play drop animation
                        return 0;
                    }
                    Grid[(pieceX + x) + (pieceY + y) * GRID_WIDTH] = *gr;
                }
            // checking for full lines
            height = pieceY + curPiece->gs;
            gr = Grid + pieceY * GRID_WIDTH;
            linesCleared = 0;
            for(y = pieceY; y <= height; y++)
            {
                isFull = 1;
                for(x = 0; x < GRID_WIDTH; x++, gr++)
                {
                    if(!*gr)
                    {
                        isFull = 0;
                    }
                }
                if(isFull)
                {
                    lineIndexes[linesCleared] = y;
                    linesCleared++;
                }
            }
            if(linesCleared)
            {
                animationFrames = animationDuration;
                SetTimer(hWnd, 2, 40, NULL);
                PlaySoundEffect(linesCleared == 4 ? EFFECT_TETRIS : EFFECT_LINECLEAR);
            }
            else
            {
                curPiece = nextPiece;
                memcpy(piece, curPiece->grid, curPiece->gs * curPiece->gs);
                nextPiece = Pieces + rand() % (sizeof(Pieces) / sizeof(*Pieces));
            }
            pieceX = GRID_WIDTH / 2 - 1;
            pieceY = 0;
            KillTimer(hWnd, 1);
            PlaySoundEffect(EFFECT_FELL);
        }
        else
        {
            pieceY++;
            // fast falling
            if(wParam == 1)
                score++;
        }
        RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE);
        break;
    case WM_PAINT:
        hdc = BeginPaint(hWnd, &ps);
        SetTextAlign(BufferDc, TA_CENTER);
        r = (RECT) { 0, 0, BufferWidth, BufferHeight };
        FillRect(BufferDc, &r, WallBrush);
        SetDCBrushColor(BufferDc, showColor ? color : 0);
        r = (RECT) { OffsetX, OffsetY, OffsetX + TileWidth * GRID_WIDTH, OffsetY + (lines % GRID_HEIGHT) * TileHeight };
        FillRect(BufferDc, &r, GetStockObject(BLACK_BRUSH));
        r.top = r.bottom;
        r.bottom = OffsetY + GRID_HEIGHT * TileHeight;
        FillRect(BufferDc, &r, GetStockObject(DC_BRUSH));
        if(!paused)
        {
            gr = Grid;
            for(y = 0; y < GRID_HEIGHT; y++)
            for(x = 0; x < GRID_WIDTH; x++, gr++)
            {
                if(!*gr)
                    continue;
                r = (RECT) { OffsetX + x * TileWidth, OffsetY + y * TileHeight,
                          OffsetX + (x + 1) * TileWidth, OffsetY + (y + 1) * TileHeight };
                StretchBlt(BufferDc, r.left, r.top, TileWidth, TileHeight, SpriteDc, 16 * (*gr - 1), 0, 16, 16, SRCINVERT);
            }
            if(gameOverY == GRID_HEIGHT)
            {
                r = (RECT) { OffsetX, OffsetY, OffsetX + GRID_WIDTH * TileWidth, OffsetY + GRID_HEIGHT * TileHeight };
                FillRect(BufferDc, &r, GetStockObject(BLACK_BRUSH));
                CDrawText(BufferDc, 2, OffsetX + GRID_WIDTH * TileWidth / 2, OffsetY + GRID_HEIGHT * TileHeight / 2, "Game over", sizeof("Game over") - 1);
                CDrawText(BufferDc, 2, OffsetX + GRID_WIDTH * TileWidth / 2 - 16, OffsetY + GRID_HEIGHT * TileHeight / 2 + 2 * 16 + 4, "Try again!", sizeof("Try again!") - 1);
                CDrawText(BufferDc, 2, OffsetX + GRID_WIDTH * TileWidth / 2 + 10 * 8, OffsetY + GRID_HEIGHT * TileHeight / 2 + 2 * 16 + 4, "[\\", 2);
                CDrawText(BufferDc, 1, OffsetX + GRID_WIDTH * TileWidth / 2, OffsetY + GRID_HEIGHT * TileHeight / 2 + 8 + 4 * 16 + 2 * 4, "Enter  Restart", sizeof("Enter  Restart") - 1);
                CDrawText(BufferDc, 1, OffsetX + GRID_WIDTH * TileWidth / 2, OffsetY + GRID_HEIGHT * TileHeight / 2 + 6 * 16 + 3 * 4, "Escape  Menu", sizeof("Escape  Menu") - 1);
            }
            if(!gameOver)
            {
                if(animationFrames >= 0)
                {
                    dx = (15 - animationFrames + 3) * GRID_WIDTH / 40;
                    dx *= TileWidth;
                    for(i = 0; i < linesCleared; i++)
                    {
                        r.top = OffsetY + lineIndexes[i] * TileHeight;
                        r.bottom = r.top + TileHeight + 1;
                        r.left = OffsetX;
                        r.right = r.left + dx;
                        FillRect(BufferDc, &r, GetStockObject(BLACK_BRUSH));
                        r.right = OffsetX + GRID_WIDTH * TileWidth;
                        r.left = r.right - dx;
                        FillRect(BufferDc, &r, GetStockObject(BLACK_BRUSH));
                    }
                }
                else
                {
                    gr = piece;
                    for(y = 0; y < curPiece->gs; y++)
                    for(x = 0; x < curPiece->gs; x++, gr++)
                    {
                        if(!*gr)
                            continue;
                        r = (RECT) { OffsetX + (pieceX + x) * TileWidth, OffsetY + (pieceY + y) * TileHeight,
                                  OffsetX + (pieceX + x + 1) * TileWidth, OffsetY + (pieceY + y + 1) * TileHeight };
                        StretchBlt(BufferDc, r.left, r.top, TileWidth, TileHeight, SpriteDc, 16 * (*gr - 1), 0, 16, 16, SRCINVERT);
                    }
                }
            }
        }
        else
        {
            r = (RECT) { OffsetX, OffsetY,
                      OffsetX + GRID_WIDTH * TileWidth, OffsetY + GRID_HEIGHT * TileHeight };
            FillRect(BufferDc, &r, GetStockObject(BLACK_BRUSH));
            CDrawText(BufferDc, 2, OffsetX + GRID_WIDTH * TileWidth / 2, OffsetY + GRID_HEIGHT * TileHeight / 2 - 30, "Hit enter", sizeof("Hit enter") - 1);
            CDrawText(BufferDc, 2, OffsetX + GRID_WIDTH * TileWidth / 2, OffsetY + GRID_HEIGHT * TileHeight / 2 + 4, "to", sizeof("to") - 1);
            CDrawText(BufferDc, 2, OffsetX + GRID_WIDTH * TileWidth / 2, OffsetY + GRID_HEIGHT * TileHeight / 2 + 38, "continue", sizeof("continue") - 1);
            CDrawText(BufferDc, 2, OffsetX + GRID_WIDTH * TileWidth / 2, OffsetY + GRID_HEIGHT * TileHeight / 2 + 72, "game", sizeof("game") - 1);
        }
        // draw score
        r = (RECT) { OffsetX + (GRID_WIDTH + 1) * TileWidth, OffsetY,
                      OffsetX + (GRID_WIDTH + 2 + 3 + 1) * TileWidth + 1, OffsetY + 60 };
        FillRect(BufferDc, &r, GetStockObject(DC_BRUSH));
        CDrawText(BufferDc, 2, (r.left + r.right) / 2, r.top, "Score", sizeof("Score") - 1);
        sprintf(numBuf, "%d", score);
        CDrawText(BufferDc, 2, (r.left + r.right) / 2, (r.top + r.bottom) / 2, numBuf, strlen(numBuf));
        // draw level
        r.top = r.bottom + 20;
        r.bottom = r.top + 90;
        FillRect(BufferDc, &r, GetStockObject(DC_BRUSH));
        CDrawText(BufferDc, 2, (r.left + r.right) / 2, r.top, "Level", sizeof("Level") - 1);
        sprintf(numBuf, "%d", level);
        CDrawText(BufferDc, 2, (r.left + r.right) / 2, (r.top + r.bottom) / 2, numBuf, strlen(numBuf));
        // draw lines
        r.top = r.bottom + 10;
        r.bottom = r.top + 90;
        FillRect(BufferDc, &r, GetStockObject(DC_BRUSH));
        CDrawText(BufferDc, 2, (r.left + r.right) / 2, r.top, "Lines", sizeof("Lines") - 1);
        sprintf(numBuf, "%d", lines);
        CDrawText(BufferDc, 2, (r.left + r.right) / 2, (r.top + r.bottom) / 2, numBuf, strlen(numBuf));
        // draw next piece
        r.top = r.bottom + 20;
        r.bottom = r.top + TileHeight * 5;
        FillRect(BufferDc, &r, GetStockObject(DC_BRUSH));
        if(!paused)
        {
            gr = nextPiece->grid;
            for(y = 0; y < nextPiece->gs; y++)
            for(x = 0; x < nextPiece->gs; x++, gr++)
            {
                if(!*gr)
                    continue;
                StretchBlt(BufferDc, r.left + (x + 1) * TileWidth, r.top + (y + 1) * TileHeight, TileWidth, TileHeight, SpriteDc, 16 * (*gr - 1), 0, 16, 16, SRCINVERT);
            }
        }

        GetClientRect(hWnd, &r);
        StretchBlt(hdc, 0, 0, r.right, r.bottom, BufferDc, 0, 0, BufferWidth, BufferHeight, SRCCOPY);
        EndPaint(hWnd, &ps);
        return 0;
    case WM_KEYDOWN:
        if(wParam > 255)
            return 0;
        if(gameOver)
        {
            if(gameOverY >= GRID_HEIGHT && (wParam == VK_RETURN || wParam == VK_ESCAPE))
            {
                paused = 0;
                level = startLevel;
                color = 0xAA8000;
                lines = 0;
                linesCleared = 0;
                animationFrames = -1;
                downTime = 7;
                gameOverY = gameOver = 0;
                if(wParam == VK_ESCAPE)
                {
                    SendMessage(MainWindow, WM_SETACTIVEWINDOW, 1, score);
                    score = 0;
                    return 0;
                }
                score = 0;
                pieceX = GRID_WIDTH / 2 - 1;
                pieceY = 0;
                curPiece = Pieces + rand() % (sizeof(Pieces) / sizeof(*Pieces));
                nextPiece = Pieces + rand() % (sizeof(Pieces) / sizeof(*Pieces));
                memcpy(piece, curPiece->grid, curPiece->gs * curPiece->gs);
                SetTimer(hWnd, 0, level >= 25 ? 20 :
                                  level >= 20 ? 30 - (level - 20) * 2 :
                                  level >= 14 ? 80 - (level - 14) * 10 :
                                  860 - level * 60, NULL);
                Audio.musicPlaying = 1;
            }
            return 0;
        }
        if(animationFrames > 0)
            return 0;
        if(paused)
        {
            if(wParam == VK_RETURN || wParam == VK_ESCAPE || wParam == 'P')
            {
                if(!keys[wParam])
                {
                    Audio.paused = paused = 0;
                    keys[wParam] = 1;
                }
            }
            return 0;
        }
        ccw = 0;
        switch(wParam)
        {
        case 'S':
        case VK_DOWN:
            if(!keys[wParam])
            {
                SetTimer(hWnd, 1, 40, NULL);
                keys[wParam] = 1;
            }
            break;
        case 'J':
            ccw = 1;
        case 'I':
        case 'W':
        case VK_UP:
            if(keys[wParam])
                return 0;
            keys[wParam] = 1;
            RotateGrid(piece, curPiece->gs, rot, ccw);
            if(!CheckCollision(rot, curPiece->gs, pieceX, pieceY))
                return 0;
            // PlaySoundEffect(EFFECT_ROTATE); --> this effect is kinda annoying
            memcpy(piece, rot, curPiece->gs * curPiece->gs);
            break;
        case 'A': case VK_LEFT: if(!CheckCollision(piece, curPiece->gs, pieceX - 1, pieceY)) return 0; pieceX--; break;
        case 'D': case VK_RIGHT: if(!CheckCollision(piece, curPiece->gs, pieceX + 1, pieceY)) return 0; pieceX++; break;
        case 'M': Audio.musicPlaying = !Audio.musicPlaying; return 0;
        case 'N': AudioStartMusic(); Audio.locked = !Audio.locked; return 0;
        case 'C': showColor = !showColor; break;
        case 'P':
        case VK_ESCAPE:
        case VK_RETURN:
            if(!keys[wParam])
            {
                Audio.paused = paused = 1;
                keys[wParam] = 1;
            }
            break;
        default:
            return 0;
        }
        RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE);
        return 0;
    case WM_KEYUP:
        if(wParam > 255)
            return 0;
        keys[wParam] = 0;
        if(wParam == 'S' || wParam == VK_DOWN)
            KillTimer(hWnd, 1);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

void RegisterClasses(void)
{
    WNDCLASS wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpszClassName = "Main";
    wc.lpfnWndProc = MainProc;
    RegisterClass(&wc);
    wc.lpszClassName = "Game";
    wc.lpfnWndProc = GameProc;
    RegisterClass(&wc);
    wc.lpszClassName = "GameMenu";
    wc.lpfnWndProc = MenuProc;
    RegisterClass(&wc);
}

int main(void)
{
    HWND hWnd;
    MSG msg;

    int i, j;
    long len;
    FILE *fp;
    float factor, inc;
    DWORD dwThreadId;

    FreeConsole();
    srand(time(NULL));
    InitPieces();
    RegisterClasses();
    hWnd = CreateWindow("Main", "Tetris", WS_VISIBLE | WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                             CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, NULL, NULL);

    // init sound
    const char *soundPaths[] = {
        "sounds\\theme.wav",
        "sounds\\Tetris_turn_effect.wav",
        "sounds\\Tetris_regular_line_clear.wav",
        "sounds\\nes-tetris-sound-effect-tetris-clear.wav",
        "sounds\\End.wav",
        "sounds\\Fell.wav"
    };

    for(i = 0; i < sizeof(soundPaths) / sizeof(*soundPaths); i++)
    {
        fp = fopen(soundPaths[i], "rb");
        if(!fp)
        {
            printf("error: failed loading sound resources: %s\n", soundPaths[i]);
            while(--i >= 0)
                free(AudioBuffers[i].data);
            break;
        }
        fseek(fp, 0, SEEK_END);
        len = ftell(fp);
        AudioBuffers[i].dataLength = len;
        AudioBuffers[i].data = malloc(len);
        fseek(fp, 44, SEEK_SET); // skip header
        fread(AudioBuffers[i].data, 1, len, fp);
        fclose(fp);
    }
    if(i > 0)
    {
        Audio.waveFormat.cbSize          = 0;
        Audio.waveFormat.wFormatTag      = WAVE_FORMAT_PCM;
        Audio.waveFormat.nChannels       = 2;
        Audio.waveFormat.nSamplesPerSec  = 8000;
        Audio.waveFormat.nAvgBytesPerSec = 8000 * 2 * 16 / 8;
        Audio.waveFormat.nBlockAlign     = 2 * 16 / 8;
        Audio.waveFormat.wBitsPerSample  = 16;
        Audio.paused = 1;
        if(!waveOutOpen(&Audio.hWaveOut,
                       WAVE_MAPPER,
                       &Audio.waveFormat,
                       (DWORD_PTR) WaveOutputCallback,
                       0, CALLBACK_FUNCTION))
        {
            // make sounds ready for mixing
            i = AudioBuffers[0].dataLength / 2;
            while(i--)
                AudioBuffers[0].data[i] /= 7;
            // create smooth transition from one end to the other ((for looping) against sound artifacts)
            factor = 0.01f;
            inc = 0.99f / 1000;
            for(i = 0; i < 1000; i++, factor += inc)
            {
                AudioBuffers[0].data[i] *= factor;
                AudioBuffers[0].data[AudioBuffers[0].dataLength / 2 - 1 - i] *= factor;
            }
            // give sound effects a fade effect (against sound artifacts that appear with sudden amplitude changes)
            for(i = 1; i <= EFFECT_MAX; i++)
            {
                j = AudioBuffers[i].dataLength / 2;
                factor = 0.01f;
                inc = 0.99f / j;
                while(j--)
                {
                    AudioBuffers[i].data[j] *= factor;
                    factor += inc;
                }
            }
            Audio.audioThread = CreateThread(NULL, 0, AudioThread, NULL, 0, &dwThreadId);
        }
        else
        {
            printf("error: failed opening wave output\n");
        }
    }

    UpdateWindow(hWnd);

    while(GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    CloseHandle(Audio.audioThread);
    waveOutClose(Audio.hWaveOut);
    return 0;
}

#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#include <Windows.h>
#include <set>
#include <SFML/Graphics.hpp>

static int TileSize = 16;

static LONG_PTR originalsfmlcallback = 0x0;

static sf::Texture * g_texture = NULL;
static bool g_textureWasReloaded = false;
static std::set<std::pair<int, int>> g_emptyTiles;
static std::wstring g_loadedfilepath;
static std::map<std::pair<int, int>, int> g_connections;
static bool g_showTexts = true;

// bit flags
enum ECONNECTION_DIRECTION {
    ECD_UP = 1,
    ECD_DOWN = 2,
    ECD_LEFT = 4,
    ECD_RIGHT = 8,
};

static int detectConnections(int tx, int ty, const sf::Image& img)
{
    const sf::Uint8  * pixels = img.getPixelsPtr();
    const int rowsize = (int)img.getSize().x;
    int ret = 0;

    for(int dy = 0; dy < TileSize; ++dy)
    {
        if(pixels[4 * (tx + 0 + (ty + dy) * rowsize) + 3] != 0)
            ret |= ECD_LEFT;

        if(pixels[4 * (tx + (TileSize - 1) + (ty + dy) * rowsize) + 3] != 0)
            ret |= ECD_RIGHT;
    }

    for(int dx = 0; dx < TileSize; ++dx)
    {
        if(pixels[4 * (tx + dx + (ty + 0) * rowsize) + 3] != 0)
            ret |= ECD_UP;

        if(pixels[4 * (tx + dx + (ty + (TileSize - 1)) * rowsize) + 3] != 0)
            ret |= ECD_DOWN;
    }

    //if(pixels[4 * (tx + dx + (ty + dy) * rowsize) + 3] != 0)

    return ret;
}

class MyFileStream : public sf::InputStream {
public:
    MyFileStream(FILE * f) : m_file(f) {}

    virtual sf::Int64 read(void * data, sf::Int64 size) { return fread(data, 1, size, m_file); }
    virtual sf::Int64 seek(sf::Int64 position) { return -1; }
    virtual sf::Int64 tell() { return -1; }
    virtual sf::Int64 getSize() { return -1; }

private:
    FILE * m_file = 0x0;
};

static bool emptySquare(int tx, int ty, const sf::Image& img)
{
    const sf::Uint8  * pixels = img.getPixelsPtr();
    const int rowsize = (int)img.getSize().x;

    for(int dy = 0; dy < TileSize; ++dy)
        for(int dx = 0; dx < TileSize; ++dx)
            if(pixels[4 * (tx + dx + (ty + dy) * rowsize) + 3] != 0)
                return false;

    return true;
}

template<typename T, typename U>
static T roundUp(T x, U multi)
{
    while(x % multi)
        ++x;
    return x;
}

// this is due to some tilesheets having some notes on the right/bottom edge but
// that do not fill full tile size, but we still want the notes to be displayed
// in the viewer, so we round up the image, this makes all the other code simpler
// because we can assume each texture is a multiple of kTileSize
static void roundUpImageSize(sf::Image& img)
{
    sf::Vector2u newsize = img.getSize();
    newsize.x = roundUp(newsize.x, TileSize);
    newsize.y = roundUp(newsize.y, TileSize);
    if(newsize == img.getSize())
        return;

    sf::Image newimage;
    newimage.create(newsize.x, newsize.y, sf::Color::Transparent);
    newimage.copy(img, 0, 0);
    img = newimage;
}

static bool tryLoadTexture(const wchar_t * filepath)
{
    g_loadedfilepath.clear();
    g_textureWasReloaded = true;

    FILE * f = _wfopen(filepath, L"rb");
    if(!f)
        return false;

    if(wcsstr(filepath, L"48x48")) TileSize = 48;
    if(wcsstr(filepath, L"32x32")) TileSize = 32;
    if(wcsstr(filepath, L"16x16")) TileSize = 16;

    MyFileStream stream(f);
    const sf::Clock clo;
    sf::Image img;
    bool ret = img.loadFromStream(stream);
    roundUpImageSize(img);
    ret = ret && g_texture->loadFromImage(img);
    g_emptyTiles.clear();
    if(ret)
    {
        printf("image = %u x %u\n", img.getSize().x, img.getSize().y);
        const sf::Vector2i tilecount = sf::Vector2i(img.getSize()) / TileSize;
        printf("tiles = %d x %d\n", tilecount.x, tilecount.y);
        for(int tx = 0; tx < tilecount.x; ++tx)
        {
            for(int ty = 0; ty < tilecount.y; ++ty)
            {
                if(emptySquare(tx * TileSize, ty * TileSize, img))
                    g_emptyTiles.insert(std::make_pair(tx, ty));

                g_connections[std::make_pair(tx, ty)] = detectConnections(tx * TileSize, ty * TileSize, img);
            }
        }
    }

    if(!ret)
        *g_texture = sf::Texture();

    std::wstring displaypath = filepath;
    // changed slash to one that is friendly to bash for copying by hand from stdout in terminal
    for(wchar_t& c : displaypath)
        if(c == L'\\')
            c = L'/';

    printf("loaded from %ls\n", displaypath.c_str());
    printf("loadFromStream time = %.3f\n", clo.getElapsedTime().asSeconds());
    fclose(f);
    if(ret)
        g_loadedfilepath = filepath;
    else
        g_loadedfilepath = L"Tile Sheet Viewer - Erorr";

    return true;
}

static LRESULT CALLBACK mycallback(HWND handle, UINT message, WPARAM wParam, LPARAM lParam)
{
    if(message == WM_DROPFILES)
    {
        HDROP hdrop = reinterpret_cast<HDROP>(wParam);
        // could do here DragQueryPoint if needed
        const UINT filescount = DragQueryFile(hdrop, 0xFFFFFFFF, NULL, 0);
        for(UINT i = 0; i < filescount; ++i)
        {
            const UINT bufsize = DragQueryFile(hdrop, i, NULL, 0);
            std::vector<wchar_t> filepath(bufsize + 1);
            if(DragQueryFile(hdrop, i, filepath.data(), bufsize + 1))
            {
                if(tryLoadTexture(filepath.data()))
                    break;
            }
        }
        DragFinish(hdrop);
    } //if WM_DROPFILES
    return CallWindowProcW(reinterpret_cast<WNDPROC>(originalsfmlcallback), handle, message, wParam, lParam);
}

int wmain(int argc, wchar_t ** argv);

#include "kFontData.txt"

int main(void)
{
    int argc = 0;
    wchar_t ** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if(!argv)
        return 1;

    const int ret = wmain(argc, argv);
    LocalFree(argv);
    return ret;
}

int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow)
{
    return main();
}

int wmain(int argc, wchar_t ** argv)
{
    sf::Texture texture;
    g_texture = &texture;


    // i think creating and loading texture before window is created creates an extra hidden
    // GL context, but thats fine, cus it makes no white unusable window appear while texture
    // loads, like doing it the old way (window first, load second, event loop third)
    if(argc > 1)
    {
        tryLoadTexture(argv[1]);
    }
    else
    {
        FILE * f = fopen("tilesheetviewer-lastfile.txt", "rb");
        if(f)
        {
            wchar_t buff[1024] = {0x0};
            fread(buff, sizeof(wchar_t), 1000, f);
            fclose(f);
            tryLoadTexture(buff);
        }
    }

    if(g_loadedfilepath.empty())
        g_loadedfilepath = L"Tile Sheet Viewer - No File";

    sf::RenderWindow app(sf::VideoMode(1024u, 768u), g_loadedfilepath);
    app.setFramerateLimit(60u);

    sf::Font font;
    font.loadFromMemory(kFontData, sizeof(kFontData));

    HWND handle = app.getSystemHandle();
    DragAcceptFiles(handle, TRUE);
    originalsfmlcallback = SetWindowLongPtrW(handle, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(mycallback));

    bool dragging = false;
    sf::Vector2f draglast;

    std::vector<sf::Text> texts;
    sf::VertexArray arr(sf::Quads);
    sf::VertexArray arr2(sf::Quads);
    sf::VertexArray arr3(sf::Lines);

    const sf::Clock animationclock;
    std::vector<sf::IntRect> frames;
    for(int i = 0; i < 6; ++i)
        frames.push_back(sf::IntRect(3 * 6 + i, 4, 1, 2));

    int fontsize = 32;

    std::wstring prevtitle;

    while(app.isOpen())
    {
        sf::Event eve;
        while(app.pollEvent(eve))
        {
            if(eve.type == sf::Event::Closed)
                app.close();

            if(eve.type == sf::Event::Resized)
            {
                sf::View view = app.getView();
                view.setSize(sf::Vector2f(sf::Vector2u(eve.size.width, eve.size.height)));
                app.setView(view);
            }

            if(eve.type == sf::Event::MouseButtonPressed)
            {
                draglast = app.mapPixelToCoords(sf::Mouse::getPosition(app));
                dragging = true;
            }

            if(eve.type == sf::Event::MouseMoved)
            {
                if(dragging)
                {
                    const auto pos = app.mapPixelToCoords(sf::Mouse::getPosition(app));
                    sf::View view = app.getView();
                    view.move(draglast - pos);
                    app.setView(view);
                    draglast = app.mapPixelToCoords(sf::Mouse::getPosition(app));
                }
            }

            if(eve.type == sf::Event::MouseButtonReleased)
            {
                dragging = false;
            }

            if(eve.type == sf::Event::MouseWheelScrolled)
            {
                auto view = app.getView();
                view.zoom(1.f - 0.1f * eve.mouseWheelScroll.delta);
                app.setView(view);
            }

            if(eve.type == sf::Event::KeyPressed && eve.key.code == sf::Keyboard::W)
            {
                fontsize = std::min(fontsize + 1, 50);
                g_textureWasReloaded = true; // to force text regeneration
            }

            if(eve.type == sf::Event::KeyPressed && eve.key.code == sf::Keyboard::Space)
                g_showTexts = !g_showTexts;

            if(eve.type == sf::Event::KeyPressed && eve.key.code == sf::Keyboard::T)
                g_showTexts = !g_showTexts;

            if(eve.type == sf::Event::KeyPressed && eve.key.code == sf::Keyboard::S)
            {
                fontsize = std::max(fontsize - 1, 0);
                g_textureWasReloaded = true; // to force text regeneration
            }


        } // while app poll event eve

        app.clear();

        const float tilesize = static_cast<float>(TileSize);
        const float spacing = tilesize / 8.f;
        const sf::Vector2i tileamount = sf::Vector2i(texture.getSize() / (unsigned)TileSize);

        if(g_textureWasReloaded)
        {
            arr3.clear();
            arr2.clear();
            arr.clear();

            printf("tiles = %d x %d\n", tileamount.x, tileamount.y);

            for(int tx = 0; tx < tileamount.x; ++tx)
            {
                for(int ty = 0; ty < tileamount.y; ++ty)
                {
                    if(g_emptyTiles.find(std::make_pair(tx, ty)) != g_emptyTiles.end())
                        continue;

                    const sf::Vector2f position = (tilesize + spacing) * sf::Vector2f(sf::Vector2i(tx, ty));
                    const sf::Vector2f coords = tilesize * sf::Vector2f(sf::Vector2i(tx, ty));

                    arr2.append(sf::Vertex(position + sf::Vector2f(0.f, 0.f), sf::Color::Magenta));
                    arr2.append(sf::Vertex(position + sf::Vector2f(tilesize, 0.f), sf::Color::Magenta));
                    arr2.append(sf::Vertex(position + sf::Vector2f(tilesize, tilesize), sf::Color::Magenta));
                    arr2.append(sf::Vertex(position + sf::Vector2f(0.f, tilesize), sf::Color::Magenta));

                    arr.append(sf::Vertex(position + sf::Vector2f(0.f, 0.f), coords + sf::Vector2f(0.f, 0.f)));
                    arr.append(sf::Vertex(position + sf::Vector2f(tilesize, 0.f), coords + sf::Vector2f(tilesize, 0.f)));
                    arr.append(sf::Vertex(position + sf::Vector2f(tilesize, tilesize), coords + sf::Vector2f(tilesize, tilesize)));
                    arr.append(sf::Vertex(position + sf::Vector2f(0.f, tilesize), coords + sf::Vector2f(0.f, tilesize)));
                }
            }
        }

        app.draw(arr2);
        app.draw(arr, &texture);

        if(g_textureWasReloaded)
        {
            texts.clear();
            for(int tx = 0; tx < tileamount.x; ++tx)
            {
                for(int ty = 0; ty < tileamount.y; ++ty)
                {
                    if(g_emptyTiles.find(std::make_pair(tx, ty)) != g_emptyTiles.end())
                        continue;

                    const sf::Vector2f position = (tilesize + spacing) * sf::Vector2f(sf::Vector2i(tx, ty));
                    const auto txt = std::to_string(tx) + "\n" + std::to_string(ty);

                    const sf::Vector2f tilemid = position + sf::Vector2f(tilesize, tilesize) / 2.f;

                    if(g_connections[std::make_pair(tx, ty)] & ECD_UP)
                    {
                        arr3.append(sf::Vertex(tilemid, sf::Color::Red));
                        arr3.append(sf::Vertex(tilemid + sf::Vector2f(0.f, -tilesize / 2.f), sf::Color::Red));
                    }

                    if(g_connections[std::make_pair(tx, ty)] & ECD_DOWN)
                    {
                        arr3.append(sf::Vertex(tilemid, sf::Color::Green));
                        arr3.append(sf::Vertex(tilemid + sf::Vector2f(0.f, +tilesize / 2.f), sf::Color::Green));
                    }

                    if(g_connections[std::make_pair(tx, ty)] & ECD_LEFT)
                    {
                        arr3.append(sf::Vertex(tilemid, sf::Color::Blue));
                        arr3.append(sf::Vertex(tilemid + sf::Vector2f(-tilesize / 2.f, 0.f), sf::Color::Blue));
                    }

                    if(g_connections[std::make_pair(tx, ty)] & ECD_RIGHT)
                    {
                        arr3.append(sf::Vertex(tilemid, sf::Color::Yellow));
                        arr3.append(sf::Vertex(tilemid + sf::Vector2f(+tilesize / 2.f, 0.f), sf::Color::Yellow));
                    }

                    sf::Text text(txt, font, fontsize);

                    const auto bounds = text.getLocalBounds();
                    const sf::Vector2f size(bounds.left + bounds.width, bounds.top + bounds.height);
                    const float scale = 0.8f * std::min(TileSize / size.x, TileSize / size.y);
                    text.setScale(scale, scale);

                    text.setOutlineThickness(3.f);
                    text.move(position);
                    texts.push_back(text);
                }
            }
        }

        const sf::FloatRect rect(app.getView().getCenter() - app.getView().getSize() / 2.f, app.getView().getSize());
        if(g_showTexts && rect.width < 3 * app.getSize().x)
        {
            for(const sf::Text& text : texts)
            {
                // TODO: improve this check a little
                if(!rect.contains(text.getPosition()))
                    continue;

                app.draw(text);
            }
        }

        g_textureWasReloaded = false;

        sf::Sprite spr;
        spr.setTexture(texture);
        const float framerate = 5.f;
        auto curframe = frames[static_cast<int>(animationclock.getElapsedTime().asSeconds() * framerate) % frames.size()];
        curframe.left *= TileSize;
        curframe.top *= TileSize;
        curframe.width *= TileSize;
        curframe.height *= TileSize;
        spr.setTextureRect(curframe);
        spr.setPosition(app.mapPixelToCoords(sf::Mouse::getPosition(app)));
        //app.draw(spr);

        if(prevtitle != g_loadedfilepath)
        {
            prevtitle = g_loadedfilepath;
            app.setTitle(g_loadedfilepath);
        }

        app.draw(arr3);
        app.display();
    }

    if(!g_loadedfilepath.empty())
    {
        FILE * f = fopen("tilesheetviewer-lastfile.txt", "wb");
        if(f)
        {
            fwrite(g_loadedfilepath.data(), sizeof(wchar_t), g_loadedfilepath.size(), f);
            fclose(f);
        }
    }

    return 0;
}

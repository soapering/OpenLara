#ifndef H_CACHE
#define H_CACHE

#include "core.h"
#include "format.h"
#include "controller.h"
#include "camera.h"

#define NO_CLIP_PLANE  1000000.0f

struct ShaderCache {
    enum Effect { FX_NONE = 0, FX_UNDERWATER = 1, FX_ALPHA_TEST = 2, FX_CLIP_PLANE = 4 };

    Shader *shaders[Core::passMAX][Shader::MAX][(FX_UNDERWATER | FX_ALPHA_TEST | FX_CLIP_PLANE) + 1];
    PSO    *pso[Core::passMAX][Shader::MAX][(FX_UNDERWATER | FX_ALPHA_TEST | FX_CLIP_PLANE) + 1][bmMAX];

    ShaderCache() {
        memset(shaders, 0, sizeof(shaders));

        LOG("shader: cache warm-up...\n");
        prepareCompose(FX_NONE);
        if (Core::settings.detail.water > Core::Settings::LOW)
            prepareCompose(FX_CLIP_PLANE);

        prepareAmbient(FX_NONE);

        if (Core::settings.detail.shadows > Core::Settings::LOW)
            prepareShadows(FX_NONE);

        if (Core::settings.detail.water > Core::Settings::LOW)
            prepareWater(FX_NONE);

        prepareFilter(FX_NONE);
        prepareGUI(FX_NONE);

        Core::resetTime();
        LOG("shader: cache is ready\n");
    }

    ~ShaderCache() {
        for (int pass = 0; pass < Core::passMAX; pass++)
            for (int type = 0; type < Shader::MAX; type++)
                for (int fx = 0; fx < sizeof(shaders[pass][Shader::MAX]) / sizeof(shaders[pass][Shader::MAX][FX_NONE]); fx++)
                    delete shaders[pass][type][fx];
    }

    #define rsBase   (RS_COLOR_WRITE | RS_DEPTH_TEST | RS_DEPTH_WRITE | RS_CULL_FRONT)
    #define rsBlend  (RS_BLEND_ALPHA | RS_BLEND_ADD)
    #define rsFull   (rsBase | rsBlend)
    #define rsShadow (RS_DEPTH_TEST | RS_DEPTH_WRITE | RS_CULL_BACK)

    void prepareCompose(int fx) {
        compile(Core::passCompose, Shader::SPRITE, fx, rsBase);

        compile(Core::passCompose, Shader::MIRROR, fx,                 rsBase);
        compile(Core::passCompose, Shader::ROOM,   fx,                 rsFull);
        compile(Core::passCompose, Shader::ROOM,   fx,                 rsFull | RS_DISCARD);
        compile(Core::passCompose, Shader::ROOM,   fx | FX_UNDERWATER, rsFull);
        compile(Core::passCompose, Shader::ROOM,   fx | FX_UNDERWATER, rsFull | RS_DISCARD);

        compile(Core::passCompose, Shader::ENTITY, fx,                 rsFull);
        compile(Core::passCompose, Shader::ENTITY, fx | FX_UNDERWATER, rsFull);
        compile(Core::passCompose, Shader::ENTITY, fx | FX_UNDERWATER, rsFull | RS_DISCARD);
        compile(Core::passCompose, Shader::ENTITY, fx,                 rsFull | RS_DISCARD);

        compile(Core::passCompose, Shader::SPRITE, fx,                 rsFull);
        compile(Core::passCompose, Shader::SPRITE, fx | FX_UNDERWATER, rsFull);
        compile(Core::passCompose, Shader::SPRITE, fx,                 rsFull | RS_DISCARD);
        compile(Core::passCompose, Shader::SPRITE, fx | FX_UNDERWATER, rsFull | RS_DISCARD);

        compile(Core::passCompose, Shader::FLASH,  fx,                 rsFull | RS_BLEND_MULT);
        compile(Core::passCompose, Shader::FLASH,  fx,                 rsFull | RS_BLEND_MULT | RS_DISCARD);
    }

    void prepareAmbient(int fx) {
        compile(Core::passAmbient, Shader::ROOM,   fx,                 rsFull);
        compile(Core::passAmbient, Shader::ROOM,   fx,                 rsFull | RS_DISCARD);
        compile(Core::passAmbient, Shader::ROOM,   fx | FX_UNDERWATER, rsFull);
        compile(Core::passAmbient, Shader::ROOM,   fx | FX_UNDERWATER, rsFull | RS_DISCARD);
        compile(Core::passAmbient, Shader::SPRITE, fx,                 rsFull | RS_DISCARD);
        compile(Core::passAmbient, Shader::SPRITE, fx | FX_UNDERWATER, rsFull | RS_DISCARD);
        compile(Core::passAmbient, Shader::FLASH,  fx,                 rsFull);
    }

    void prepareShadows(int fx) {
        compile(Core::passShadow, Shader::ENTITY, fx, rsShadow);
        compile(Core::passShadow, Shader::ENTITY, fx, rsShadow | RS_DISCARD);
        compile(Core::passShadow, Shader::MIRROR, fx, rsShadow);
        compile(Core::passShadow, Shader::MIRROR, fx, rsShadow | RS_DISCARD);
    }

    void prepareWater(int fx) {
        compile(Core::passWater, Shader::WATER_MASK,     fx, RS_COLOR_WRITE_A | RS_DEPTH_TEST);
        compile(Core::passWater, Shader::WATER_STEP,     fx, RS_COLOR_WRITE);
        compile(Core::passWater, Shader::WATER_DROP,     fx, RS_COLOR_WRITE);
        compile(Core::passWater, Shader::WATER_CAUSTICS, fx, RS_COLOR_WRITE);
        compile(Core::passWater, Shader::WATER_COMPOSE,  fx, RS_COLOR_WRITE | RS_DEPTH_TEST);
    }

    void prepareFilter(int fx) {
        compile(Core::passFilter, Shader::FILTER_UPSCALE,    fx, RS_COLOR_WRITE);
        compile(Core::passFilter, Shader::FILTER_DOWNSAMPLE, fx, RS_COLOR_WRITE);
        compile(Core::passFilter, Shader::FILTER_GRAYSCALE,  fx, RS_COLOR_WRITE);
        compile(Core::passFilter, Shader::FILTER_BLUR,       fx, RS_COLOR_WRITE);
    }

    void prepareGUI(int fx) {
        compile(Core::passGUI, Shader::DEFAULT, fx, RS_COLOR_WRITE | RS_BLEND_ALPHA);
    }

    #undef rsBase
    #undef rsBlend
    #undef rsFull
    #undef rsShadow

    Shader* compile(Core::Pass pass, Shader::Type type, int fx, uint32 rs) {
        if (rs & RS_DISCARD)
            fx |= FX_ALPHA_TEST;

    #ifndef FFP
        if (shaders[pass][type][fx])
            return shaders[pass][type][fx];

        int def[SD_MAX], defCount = 0;

        #define SD_ADD(x) (def[defCount++] = SD_##x)

        if (Core::settings.detail.shadows) {
            if (Core::support.shadowSampler) {
                SD_ADD(SHADOW_SAMPLER);
            } else {
                if (Core::support.depthTexture)
                    SD_ADD(SHADOW_DEPTH);
                else
                    SD_ADD(SHADOW_COLOR);
            }
        }

        switch (pass) {
            case Core::passCompose    :
            case Core::passShadow     :
            case Core::passAmbient    : {
                def[defCount++] = SD_TYPE_SPRITE + type;

                if (fx & FX_UNDERWATER) SD_ADD(UNDERWATER);
                if (fx & FX_ALPHA_TEST) SD_ADD(ALPHA_TEST);

                if (pass == Core::passCompose) {
                    if (fx & FX_CLIP_PLANE)
                        SD_ADD(CLIP_PLANE);
                    if (Core::settings.detail.lighting > Core::Settings::MEDIUM && (type == Shader::ENTITY))
                        SD_ADD(OPT_AMBIENT);
                    if (Core::settings.detail.shadows  > Core::Settings::LOW && (type == Shader::ENTITY || type == Shader::ROOM))
                        SD_ADD(OPT_SHADOW);
                    if (Core::settings.detail.shadows  > Core::Settings::MEDIUM && (type == Shader::ROOM))
                        SD_ADD(OPT_CONTACT);
                    if (Core::settings.detail.water    > Core::Settings::MEDIUM && (type == Shader::ENTITY || type == Shader::ROOM) && (fx & FX_UNDERWATER))
                        SD_ADD(OPT_CAUSTICS);
                }
                break;
            }
            case Core::passWater   : def[defCount++] = SD_WATER_DROP + type;     break;
            case Core::passFilter  : def[defCount++] = SD_FILTER_UPSCALE + type; break;
            case Core::passGUI     : break;
            default : ASSERT(false);
        }

        #undef SD_ADD

        LOG("shader: %s(%d) %s%s%s\n", Core::passNames[pass], type, (fx & FX_UNDERWATER) ? "u" : "", (fx & FX_ALPHA_TEST) ? "a" : "", (fx & FX_CLIP_PLANE) ? "c" : "");
        return shaders[pass][type][fx] = new Shader(pass, type, def, defCount);
    #else
        return NULL;
    #endif
    }

    Shader *getShader(Core::Pass pass, Shader::Type type, int fx) {
        Shader *shader = shaders[pass][type][fx];
    #ifndef FFP
        if (shader == NULL)
            LOG("! NULL shader: %d %d %d\n", int(pass), int(type), int(fx));
        ASSERT(shader != NULL);
    #endif
        return shader;
    }

    void bind(Core::Pass pass, Shader::Type type, int fx) {
        Core::pass = pass;

        Shader *shader = getShader(pass, type, fx);
        if (shader)
            shader->setup();

        Core::setAlphaTest((fx & FX_ALPHA_TEST) != 0);
    }
};

struct AmbientCache {
    IGame     *game;
    TR::Level *level;

    struct Cube {
        enum int32 {
            BLANK, WAIT, READY
        }    status;
        vec4 colors[6]; // TODO: ubyte4[6]
    } *items;
    int *offsets;

    struct Task {
        int  room;
        int  flip;
        int  sector;
        Cube *cube;
    } tasks[32];
    int tasksCount;

    Texture *textures[6 * 4]; // 64, 16, 4, 1 

    AmbientCache(IGame *game) : game(game), level(game->getLevel()), tasksCount(0) {
        items   = NULL;
        offsets = new int[level->roomsCount];
        int sectors = 0;
        for (int i = 0; i < level->roomsCount; i++) {
            TR::Room &r = level->rooms[i];
            offsets[i] = sectors;
            sectors += r.xSectors * r.zSectors * (r.alternateRoom > -1 ? 2 : 1); // x2 for flipped rooms
        }
    // init cache buffer
        items = new Cube[sectors];
        memset(items, 0, sizeof(Cube) * sectors);
    // init downsample textures
        for (int j = 0; j < 6; j++)
            for (int i = 0; i < 4; i++)
                textures[j * 4 + i] = new Texture(64 >> (i << 1), 64 >> (i << 1), FMT_RGBA, OPT_TARGET | OPT_NEAREST);
    }

    ~AmbientCache() {
        delete[] items;
        delete[] offsets;
        for (int i = 0; i < 6 * 4; i++)
            delete textures[i];
    }

    void addTask(int room, int sector) {
        if (tasksCount >= COUNT(tasks)) return;

        Task &task  = tasks[tasksCount++];
        task.room   = room;
        task.flip   = level->state.flags.flipped && level->rooms[room].alternateRoom > -1;
        task.sector = sector;
        task.cube   = &items[offsets[room] + sector];
        task.cube->status = Cube::WAIT;
    }

    void renderAmbient(int room, int sector, vec4 *colors) {
        PROFILE_MARKER("PASS_AMBIENT");
                
        TR::Room &r = level->rooms[room];
        TR::Room::Sector &s = r.sectors[sector];
            
        vec3 pos = vec3(float((sector / r.zSectors) * 1024 + 512 + r.info.x), 
                        float(max((s.floor - 2) * 256, (s.floor + s.ceiling) * 256 / 2)),
                        float((sector % r.zSectors) * 1024 + 512 + r.info.z));

        Core::setClearColor(vec4(0, 0, 0, 1));

        // first pass - render environment from position (room geometry & static meshes)
        game->renderEnvironment(room, pos, textures, 4);

        // second pass - downsample it
        Core::setDepthTest(false);

        game->setShader(Core::passFilter, Shader::FILTER_DOWNSAMPLE);

        for (int i = 1; i < 4; i++) {
            int size = 64 >> (i << 1);

            Core::active.shader->setParam(uParam, vec4(1.0f / (size << 2), 0.0f, 0.0f, 0.0f));

            for (int j = 0; j < 6; j++) {
                Texture *src = textures[j * 4 + i - 1];
                Texture *dst = textures[j * 4 + i];
                Core::setTarget(dst, RT_STORE_COLOR);
                src->bind(sDiffuse);
                game->getMesh()->renderQuad();
            }
        }

        // get result color from 1x1 textures
        for (int j = 0; j < 6; j++) {
            Core::setTarget(textures[j * 4 + 3], RT_LOAD_COLOR);
            colors[j] = Core::copyPixel(0, 0);
        }

        Core::setDepthTest(true);
        Core::setClearColor(vec4(0, 0, 0, 0));
    }

    void processQueue() {
        game->setupBinding();

        for (int i = 0; i < tasksCount; i++) {
            Task &task = tasks[i];
            
            bool needFlip = task.flip != level->state.flags.flipped;
           
            if (needFlip) game->flipMap(false);

            int sector = task.sector;
            if (task.flip) {
                TR::Room &r = level->rooms[task.room];
                sector -= r.xSectors * r.zSectors;
            }

            renderAmbient(task.room, sector, &task.cube->colors[0]);
            if (needFlip) game->flipMap(false);

            task.cube->status = Cube::READY;
        }
        tasksCount = 0;
    }

    Cube* getAmbient(int roomIndex, int sector) {
        TR::Room &r = level->rooms[roomIndex];
        if (level->state.flags.flipped && r.alternateRoom > -1)
            sector += r.xSectors * r.zSectors;

        Cube *cube = &items[offsets[roomIndex] + sector];
        if (cube->status == Cube::BLANK)
            addTask(roomIndex, sector);

        return cube->status == Cube::READY ? cube : NULL;
    }

    void getAmbient(int room, const vec3 &pos, Cube &value) {
        TR::Room &r = level->rooms[room];
            
        int sx = clamp((int(pos.x) - r.info.x) / 1024, 0, r.xSectors - 1);
        int sz = clamp((int(pos.z) - r.info.z) / 1024, 0, r.zSectors - 1);
            
        int sector = sx * r.zSectors + sz;
        Cube *a = getAmbient(room, sector);
        if (a)
            value = *a;
        else
            value.status = Cube::BLANK;
    }
};

struct WaterCache {
    #define MAX_SURFACES       16
    #define MAX_INVISIBLE_TIME 5.0f
    #define SIMULATE_TIMESTEP  (1.0f / 40.0f)
    #define DETAIL             (64.0f / 1024.0f)
    #define MAX_DROPS          32

    IGame     *game;
    TR::Level *level;
    Texture   *screen;
    Texture   *refract;
    Texture   *reflect;

    struct Item {
        int     from, to, caust;
        float   timer;
        float   waterLevel;
        bool    flip;
        bool    visible;
        bool    blank;
        vec3    pos, size;
        Texture *mask;
        Texture *caustics;
    #ifdef BLUR_CAUSTICS
        Texture *caustics_tmp;
    #endif
        Texture *data[2];

        Item() {
            mask = caustics = data[0] = data[1] = NULL;
        }

        Item(int from, int to) : from(from), to(to), caust(to), timer(SIMULATE_TIMESTEP), visible(true), blank(true) {
            mask = caustics = data[0] = data[1] = NULL;
        }

        void init(IGame *game) {
            TR::Level *level = game->getLevel();
            TR::Room &r = level->rooms[to]; // underwater room
            ASSERT(r.flags.water);
            int minX = r.xSectors, minZ = r.zSectors, maxX = 0, maxZ = 0;
            
            int posY = level->rooms[to].waterLevel;
            if (posY == -1)
                posY = level->rooms[from].waterLevel;

            ASSERT(posY != -1); // underwater room without reaching the surface
            
            int caustY = posY;
            
            for (int z = 0; z < r.zSectors; z++)
                for (int x = 0; x < r.xSectors; x++) {
                    TR::Room::Sector &s = r.sectors[x * r.zSectors + z];
                    if (s.roomAbove != TR::NO_ROOM && !level->rooms[s.roomAbove].flags.water) {
                        minX = min(minX, x);
                        minZ = min(minZ, z);
                        maxX = max(maxX, x);
                        maxZ = max(maxZ, z);
                        if (s.roomBelow != TR::NO_ROOM) {
                            int16 caustRoom = s.roomBelow;
                            int floor = int(level->getFloor(&s, vec3(float(r.info.x + x * 1024), float(posY), float(r.info.z + z * 1024)), &caustRoom));
                            if (floor > caustY) {
                                caustY = floor;
                                caust  = caustRoom;
                            }
                        }
                    }
                }
            maxX++;
            maxZ++;

            int w = nextPow2(maxX - minX);
            int h = nextPow2(maxZ - minZ);

            uint16 *m = new uint16[w * h];
            memset(m, 0, w * h * sizeof(m[0]));

            for (int z = minZ; z < maxZ; z++)
                for (int x = minX; x < maxX; x++) {
                    TR::Room::Sector &s = r.sectors[x * r.zSectors + z];

                    bool hasWater = s.roomAbove != TR::NO_ROOM && !level->rooms[s.roomAbove].flags.water;
                    if (hasWater) {
                        TR::Room &rt = level->rooms[s.roomAbove];
                        int xt = int(r.info.x + x * 1024 - rt.info.x) / 1024;
                        int zt = int(r.info.z + z * 1024 - rt.info.z) / 1024;
                        TR::Room::Sector &st = rt.sectors[xt * rt.zSectors + zt];
                        hasWater = s.ceiling > st.ceiling; // TODO fix for LEVEL10A, use slant
                    }

                    m[(x - minX) + w * (z - minZ)] = hasWater ? 0xF800 : 0;
                }
            mask = new Texture(w, h, FMT_RGB16, OPT_NEAREST, m);
            delete[] m;

            size = vec3(float((maxX - minX) * 512), 1.0f, float((maxZ - minZ) * 512)); // half size
            pos  = vec3(r.info.x + minX * 1024 + size.x, float(posY), r.info.z + minZ * 1024 + size.z);

            int *mf = new int[4 * w * 64 * h * 64];
            memset(mf, 0, sizeof(int) * 4 * w * 64 * h * 64);
            data[0] = new Texture(w * 64, h * 64, FMT_RGBA_HALF, OPT_TARGET | OPT_VERTEX, mf);
            data[1] = new Texture(w * 64, h * 64, FMT_RGBA_HALF, OPT_TARGET | OPT_VERTEX);
            delete[] mf;

            caustics = Core::settings.detail.water > Core::Settings::MEDIUM ? new Texture(512, 512, FMT_RGBA, OPT_TARGET) : NULL;
            #ifdef BLUR_CAUSTICS
                caustics_tmp = Core::settings.detail.water > Core::Settings::MEDIUM ? new Texture(512, 512, Texture::RGBA) : NULL;
            #endif
            
            blank = false;
        }

        void deinit() {
            delete data[0];
            delete data[1];
            delete caustics;
        #ifdef BLUR_CAUSTICS
            delete caustics_tmp;
        #endif
            delete mask;
            mask = caustics = data[0] = data[1] = NULL;
        }

    } items[MAX_SURFACES];
    int count, visible;

    int dropCount;
    struct Drop {
        vec3  pos;
        float radius;
        float strength;
        Drop() {}
        Drop(const vec3 &pos, float radius, float strength) : pos(pos), radius(radius), strength(strength) {}
    } drops[MAX_DROPS];

    WaterCache(IGame *game) : game(game), level(game->getLevel()), screen(NULL), refract(NULL), count(0), dropCount(0) {
        reflect = new Texture(512, 512, FMT_RGBA, OPT_TARGET);
    }

    ~WaterCache() {
        delete screen;
        delete refract;
        delete reflect;
        for (int i = 0; i < count; i++)
            items[i].deinit();
    }

    void update() {
        int i = 0;
        while (i < count) {
            Item &item = items[i];
            if (item.timer > MAX_INVISIBLE_TIME) {
                items[i].deinit();
                items[i] = items[--count];
                continue;
            }
            item.timer += Core::deltaTime;
            i++;
        }
    }

    void reset() {
        for (int i = 0; i < count; i++)
            items[i].visible = false;
        visible = 0;
    }

    void flipMap() {
        for (int i = 0; i < level->roomsCount && count; i++)
            if (level->rooms[i].alternateRoom > -1) {
                int j = 0;
                while (j < count) {
                    if (items[j].from == i || items[j].to == i) {
                        items[j].deinit();
                        items[j] = items[--count];
                    } else
                        j++;
                }
            }
    }

    void setVisible(int roomIndex, int nextRoom = TR::NO_ROOM) {
        if (nextRoom == TR::NO_ROOM) { // setVisible(underwaterRoom) for caustics update
            for (int i = 0; i < count; i++)
                if (items[i].caust == roomIndex) {
                    nextRoom = items[i].from;
                    if (!items[i].visible) {
                        items[i].visible = true;
                        visible++;
                    }
                    break;
                }
            return;
        }

        int from, to; // from surface room to underwater room
        if (level->rooms[roomIndex].flags.water) {
            from = nextRoom;
            to   = roomIndex;
        } else {
            from = roomIndex;
            to   = nextRoom;
        }

        if (level->rooms[to].waterLevel == -1 && level->rooms[from].waterLevel == -1) // not have water surface
            return;

        for (int i = 0; i < count; i++) {
            Item &item = items[i];
            if (item.from == from && item.to == to) {
                if (!item.visible) {
                    visible++;
                    item.visible = true;
                }
                return;
            }
        }
        if (count == MAX_SURFACES) return;

        items[count++] = Item(from, to);
        visible++;
    }

    void bindCaustics(int roomIndex) {
        Item *item = NULL;
        for (int i = 0; i < count; i++)
            if (items[i].caust == roomIndex) {
                item = &items[i];
                break;
            }

        if (item && item->caustics) {
            item->caustics->bind(sReflect);
            Core::active.shader->setParam(uRoomSize, vec4(item->pos.x - item->size.x, item->pos.z - item->size.z, item->size.x * 2.0f, item->size.z * 2.0f));
            game->setWaterParams(item->pos.y);
        } else {
            Core::active.shader->setParam(uRoomSize, vec4(0, 0, 1, 1));
            Core::blackTex->bind(sReflect);
        }
    }

    void addDrop(const vec3 &pos, float radius, float strength) {
        if (dropCount >= MAX_DROPS) return;
        drops[dropCount++] = Drop(pos, radius, strength);
    }

    void drop(Item &item) { 
        if (!dropCount) return;

        vec2 s(item.size.x * DETAIL * 2.0f, item.size.z * DETAIL * 2.0f);

        game->setShader(Core::passWater, Shader::WATER_DROP);

        vec4 rPosScale[2] = { vec4(0.0f), vec4(1.0f) };
        Core::active.shader->setParam(uPosScale, rPosScale[0], 2);
        Core::active.shader->setParam(uTexParam, vec4(1.0f / item.data[0]->width, 1.0f / item.data[0]->height, s.x / item.data[0]->width, s.y / item.data[0]->height));
            
        for (int i = 0; i < dropCount; i++) {
            Drop &drop = drops[i];

            vec3 p;
            p.x = (drop.pos.x - (item.pos.x - item.size.x)) * DETAIL;
            p.z = (drop.pos.z - (item.pos.z - item.size.z)) * DETAIL;

            Core::active.shader->setParam(uParam, vec4(p.x, p.z, drop.radius * DETAIL, -drop.strength));

            item.data[0]->bind(sDiffuse);
            Core::setTarget(item.data[1], RT_STORE_COLOR);
            Core::setViewport(0, 0, int(s.x + 0.5f), int(s.y + 0.5f));
            game->getMesh()->renderQuad();
            swap(item.data[0], item.data[1]);
        }
    }
    
    void step(Item &item) {
        if (item.timer < SIMULATE_TIMESTEP) return;

        vec2 s(item.size.x * DETAIL * 2.0f, item.size.z * DETAIL * 2.0f);

        game->setShader(Core::passWater, Shader::WATER_STEP);
        Core::active.shader->setParam(uParam, vec4(0.995f, 1.0f, 0, Core::params.x));
        Core::active.shader->setParam(uTexParam, vec4(1.0f / item.data[0]->width, 1.0f / item.data[0]->height, s.x / item.data[0]->width, s.y / item.data[0]->height));
            
        while (item.timer >= SIMULATE_TIMESTEP) {
        // water step
            item.data[0]->bind(sDiffuse);
            Core::setTarget(item.data[1], RT_STORE_COLOR);
            Core::setViewport(0, 0, int(s.x + 0.5f), int(s.y + 0.5f));
            game->getMesh()->renderQuad();
            swap(item.data[0], item.data[1]);
            item.timer -= SIMULATE_TIMESTEP;
        }

        if (Core::settings.detail.water < Core::Settings::HIGH)
            return;

    // calc caustics
        game->setShader(Core::passWater, Shader::WATER_CAUSTICS);
        vec4 rPosScale[2] = { vec4(0.0f), vec4(32767.0f / PLANE_DETAIL) };
        Core::active.shader->setParam(uPosScale, rPosScale[0], 2);

        float sx = item.size.x * DETAIL / (item.data[0]->width  / 2);
        float sz = item.size.z * DETAIL / (item.data[0]->height / 2);

        Core::active.shader->setParam(uTexParam, vec4(0.0f, 0.0f, sx, sz));

        Core::whiteTex->bind(sReflect);
        item.data[0]->bind(sNormal);
        Core::setTarget(item.caustics, RT_CLEAR_COLOR | RT_STORE_COLOR);
        Core::setViewport(1, 1, item.caustics->width - 2, item.caustics->width - 2); // leave 1px for black border
        game->getMesh()->renderPlane();
    #ifdef BLUR_CAUSTICS
        // v blur
        Core::setTarget(item.caustics_tmp, CLEAR_ALL);
        game->setShader(Core::passFilter, Shader::FILTER_BLUR, false, false);
        Core::active.shader->setParam(uParam, vec4(0, 1, 1.0f / item.caustics->width, 0));;
        item.caustics->bind(sDiffuse);
        game->getMesh()->renderQuad();
        Core::invalidateTarget(false, true);

        // h blur
        Core::setTarget(item.caustics, CLEAR_ALL);
        game->setShader(Core::passFilter, Shader::FILTER_BLUR, false, false);
        Core::active.shader->setParam(uParam, vec4(1, 0, 1.0f / item.caustics->width, 0));;
        item.caustics_tmp->bind(sDiffuse);
        game->getMesh()->renderQuad();
        Core::invalidateTarget(false, true);
    #endif
    }

    void renderMask() {
        if (!visible) return;
        PROFILE_MARKER("WATER_RENDER_MASK");
    // mask underwater geometry by zero alpha
        game->setShader(Core::passWater, Shader::WATER_MASK);
        Core::active.shader->setParam(uTexParam, vec4(1.0f));

        Core::setColorWrite(false, false, false, true);
        Core::setDepthWrite(false);
        Core::setCullMode(cmNone);

        for (int i = 0; i < count; i++) {
            Item &item = items[i];
            if (!item.visible) continue;

            vec4 rPosScale[2] = { vec4(item.pos, 0.0f), vec4(item.size, 1.0) };
            Core::active.shader->setParam(uPosScale, rPosScale[0], 2);

            game->getMesh()->renderQuad();
        }

        Core::setColorWrite(true, true, true, true);
        Core::setDepthWrite(true);
        Core::setCullMode(cmFront);
    }


    Texture* getScreenTex() {
        int w = Core::viewportDef.width;
        int h = Core::viewportDef.height;
    // get refraction texture
        if (!refract || w != refract->origWidth || h != refract->origHeight) {
            PROFILE_MARKER("WATER_REFRACT_INIT");
            delete refract;
            refract = new Texture(w, h, FMT_RGBA, OPT_TARGET);
        #ifdef _OS_IOS
            delete screen;
            screen  = new Texture(w, h, FMT_RGBA, OPT_TARGET);
        #endif
        }
        return screen;
    }

    void copyScreenToRefract() {
        PROFILE_MARKER("WATER_REFRACT_COPY");
    // get refraction texture
        int x, y;
        if (!screen) {
            x = Core::viewportDef.x;
            y = Core::viewportDef.y;
        } else
            x = y = 0;

        Core::copyTarget(refract, 0, 0, x, y, Core::viewportDef.width, Core::viewportDef.height); // copy framebuffer into refraction texture
    }

    void simulate() {
       PROFILE_MARKER("WATER_SIMULATE");
    // simulate water
        Core::setDepthTest(false);
        Core::setBlendMode(bmNone);
        for (int i = 0; i < count; i++) {
            Item &item = items[i];
            if (!item.visible) continue;

            if (item.timer >= SIMULATE_TIMESTEP || dropCount) {
                item.mask->bind(sMask);
            // add water drops
                drop(item);                    
            // simulation step
                step(item);
            }
        }
        Core::setDepthTest(true);
    }

    void renderReflect() {
        if (!visible) return;
        PROFILE_MARKER("WATER_REFLECT");

        for (int i = 0; i < count; i++) {
            Item &item = items[i];
            if (item.visible && item.blank)
                item.init(game);
        }

    // render mirror reflection
        Core::setTarget(reflect, RT_CLEAR_COLOR | RT_CLEAR_DEPTH | RT_STORE_COLOR);
        Camera *camera = (Camera*)game->getCamera();
        game->setupBinding();

    // merge visible rooms for all items
        int roomsList[256];
        int roomsCount = 0;

        for (int i = 0; i < level->roomsCount; i++)
            level->rooms[i].flags.visible = false;

        bool underwater = level->rooms[camera->getRoomIndex()].flags.water;
        vec4 reflectPlane;

        for (int i = 0; i < count; i++) {
            Item &item = items[i];
            if (!item.visible) continue;

            reflectPlane = vec4(0, 1, 0, -item.pos.y);
            camera->reflectPlane = &reflectPlane;
            camera->setup(true);

            game->getVisibleRooms(roomsList, roomsCount, TR::NO_ROOM, underwater ? item.from : item.to, vec4(-1.0f, -1.0f, 1.0f, 1.0f), false);
        }

        if (roomsCount) {
        // select optimal water plane
            float waterDist  = 10000000.0f;
            int   waterItem = 0;

            for (int i = 0; i < count; i++) {
                Item &item = items[i];
                if (!item.visible) continue;

                float d = fabsf(item.pos.x - camera->eye.pos.x) + fabsf(item.pos.z - camera->eye.pos.z);

                if (d < waterDist) {
                    waterDist = d;
                    waterItem = i;
                }
            }

            float waterLevel = items[waterItem].pos.y;

            reflectPlane = vec4(0, 1, 0, -waterLevel);
            camera->reflectPlane = &reflectPlane;
            camera->setup(true);

        // render reflections frame
            float sign = underwater ? -1.0f : 1.0f;
            game->setClipParams(sign, waterLevel * sign);
            game->renderView(TR::NO_ROOM, false, false, roomsCount, roomsList);
        }

        game->setClipParams(1.0f, NO_CLIP_PLANE);

        camera->reflectPlane = NULL;
        camera->setup(true);
    }

    void render() {
        if (!visible) return;
        PROFILE_MARKER("WATER_RENDER");
        for (int i = 0; i < count; i++) {
            Item &item = items[i];
            if (!item.visible) continue;

        // render water plane
            game->setShader(Core::passWater, Shader::WATER_COMPOSE);
            Core::active.shader->setParam(uLightPos,    Core::lightPos[0],   1);
            Core::active.shader->setParam(uLightColor,  Core::lightColor[0], 1);

            Core::active.shader->setParam(uParam, vec4(float(refract->origWidth) / refract->width, float(refract->origHeight) / refract->height, 0.05f, 0.03f));

            float sx = item.size.x * DETAIL / (item.data[0]->width  / 2);
            float sz = item.size.z * DETAIL / (item.data[0]->height / 2);

            Core::active.shader->setParam(uTexParam, vec4(0.0f, 0.0f, sx, sz));

            refract->bind(sDiffuse);
            reflect->bind(sReflect);
            item.mask->bind(sMask);
            item.data[0]->bind(sNormal);
            Core::setCullMode(cmNone);
            Core::setBlendMode(bmAlpha);
            #ifdef WATER_USE_GRID
                vec4 rPosScale[2] = { vec4(item.pos, 0.0f), vec4(item.size * vec3(1.0f / PLANE_DETAIL, 512.0f, 1.0f / PLANE_DETAIL), 1.0f) };
                Core::active.shader->setParam(uPosScale, rPosScale[0], 2);
                game->getMesh()->renderPlane();
            #else
                vec4 rPosScale[2] = { vec4(item.pos, 0.0f), vec4(item.size, 1.0) };
                Core::active.shader->setParam(uPosScale, rPosScale[0], 2);
                game->getMesh()->renderQuad();
            #endif
            Core::setCullMode(cmFront);
            Core::setBlendMode(bmNone);
        }
        dropCount = 0;
    }

    void blitScreen() {
        ASSERT(screen);

        Core::setDepthTest(false);
        Core::setBlendMode(bmNone);

        game->setShader(Core::passGUI, Shader::DEFAULT);

        Core::mView.identity();
        Core::mProj = GAPI::ortho(0.0f, float(screen->origWidth), 0.0f, float(screen->origHeight), 0.0f, 1.0f);
        Core::setViewProj(Core::mView, Core::mProj);
        Core::active.shader->setParam(uViewProj, Core::mViewProj);
        Core::active.shader->setParam(uMaterial, vec4(1.0f));

        screen->bind(0);
        int w = screen->width;
        int h = screen->height;

        Index  indices[6] = { 0, 1, 2, 0, 2, 3 };
        Vertex vertices[4];
        vertices[0].coord = short4(0, h, 0, 0);
        vertices[1].coord = short4(w, h, 0, 0);
        vertices[2].coord = short4(w, 0, 0, 0);
        vertices[3].coord = short4(0, 0, 0, 0);
        vertices[0].light =
        vertices[1].light =
        vertices[2].light =
        vertices[3].light = ubyte4(255, 255, 255, 255);
        vertices[0].texCoord = short4(    0, 32767, 0, 0);
        vertices[1].texCoord = short4(32767, 32767, 0, 0);
        vertices[2].texCoord = short4(32767,     0, 0, 0);
        vertices[3].texCoord = short4(    0,     0, 0, 0);

        game->getMesh()->renderBuffer(indices, COUNT(indices), vertices, COUNT(vertices));

        Core::setDepthTest(true);
    }

    #undef MAX_WATER_SURFACES
    #undef MAX_WATER_INVISIBLE_TIME
    #undef WATER_SIMULATE_TIMESTEP
    #undef DETAIL
};

struct ZoneCache {

    struct Item {
        uint16 zone;
        uint16 count;
        uint16 *zones;
        uint16 *boxes;
        Item   *next;

        Item(uint16 zone, uint16 count, uint16 *zones, uint16 *boxes, Item *next) :
            zone(zone), count(count), zones(zones), boxes(boxes), next(next) {}

        ~Item() {
            delete[] boxes;
            delete next;
        }
    } *items;

    IGame  *game;
    // dummy arrays for path search
    uint16 *nodes;
    uint16 *parents;
    uint16 *weights;

    ZoneCache(IGame *game) : items(NULL), game(game) {
        TR::Level *level = game->getLevel();
        nodes   = new uint16[level->boxesCount * 3];
        parents = nodes + level->boxesCount;
        weights = nodes + level->boxesCount * 2;
    }

    ~ZoneCache() {
        delete   items;
        delete[] nodes;
    }

    Item *getBoxes(uint16 zone, uint16 *zones) {
        Item *item = items;
        while (item) {
            if (item->zone == zone && item->zones == zones) 
                return item;
            item = item->next;
        }

        int count = 0;
        TR::Level *level = game->getLevel();
        for (int i = 0; i < level->boxesCount; i++)
            if (zones[i] == zone)
                nodes[count++] = i;

        ASSERT(count > 0);
        uint16 *boxes = new uint16[count];
        memcpy(boxes, nodes, sizeof(uint16) * count);

        return items = new Item(zone, count, zones, boxes, items);
    }

    uint16 findPath(int ascend, int descend, bool big, int boxStart, int boxEnd, uint16 *zones, uint16 **boxes) {
        if (boxStart == 0xFFFF || boxEnd == 0xFFFF)
            return 0;

        TR::Level *level = game->getLevel();
        memset(parents, 0xFF, sizeof(uint16) * level->boxesCount); // fill parents by 0xFFFF
        memset(weights, 0x00, sizeof(uint16) * level->boxesCount); // zeroes weights

        uint16 count = 0;
        nodes[count++] = boxEnd;

        uint16 zone = zones[boxStart];

        if (zone != zones[boxEnd])
            return 0;

        TR::Box &b = level->boxes[boxStart];

        int sx = (b.minX + b.maxX) >> 11; // box center / 1024
        int sz = (b.minZ + b.maxZ) >> 11;

        while (count) {
            // get min weight
            int minI  = 0;
            int minW = weights[nodes[minI]];
            for (int i = 1; i < count; i++)
                if (weights[nodes[i]] < minW) {
                    minI = i;
                    minW = weights[nodes[i]];
                }
            int cur = nodes[minI];

            // peek min weight item from array
            count--;
            for (int i = minI; i < count; i++)
                nodes[i] = nodes[i + 1];

            // check for end of path
            if (cur == boxStart) {
                count = 0;
                while (cur != boxEnd) {
                    nodes[count++] = cur;
                    cur = parents[cur];
                }
                nodes[count++] = cur;
                *boxes = nodes;
                return count;
            }

            // add overlap boxes
            TR::Box &b = game->getLevel()->boxes[cur];
            TR::Overlap *overlap = &level->overlaps[b.overlap.index];

            do {
                uint16 index = overlap->boxIndex;
                // unvisited yet
                if (parents[index] != 0xFFFF)
                    continue;
                // has same zone
                if (zones[index] != zone)
                    continue;
                // check passability
                if (big && level->boxes[index].overlap.blockable)
                    continue;
                // check blocking (doors)
                if (level->boxes[index].overlap.block)
                    continue;
                // check for height difference
                int d = level->boxes[index].floor - b.floor;
                if (d > ascend || d < descend)
                    continue;
                
                int dx = sx - ((b.minX + b.maxX) >> 11);
                int dz = sz - ((b.minZ + b.maxZ) >> 11);
                int w = abs(dx) + abs(dz);

                ASSERT(count < level->boxesCount);
                nodes[count++] = index;
                parents[index] = cur;
                weights[index] = weights[cur] + w;

            } while (!(overlap++)->end);
        }

        return 0;
    }
};

#undef UNDERWATER_COLOR

#endif

#include "Engine/LuxEngine.hpp"
#include "Engine/Platform/Branding.hpp"
#include <stb_image.h>
#include <SDL3/SDL.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <array>
#include <stdexcept>
#include <type_traits>
#include <thread>
#include <chrono>
#include <string_view>

namespace {
int checks = 0;
void Check(bool ok, const char* message) { ++checks; if (!ok) throw std::runtime_error(message); }
template<class F> void Throws(F fn, const char* message) { bool threw = false; try { fn(); } catch (const std::exception&) { threw = true; } Check(threw,message); }
bool Near(float a, float b) { return std::abs(a-b) < 0.0001f; }
struct Files {
    std::filesystem::path root = std::filesystem::temp_directory_path() / ("luxengine-tests-" + std::to_string(SDL_GetTicksNS()));
    Files() { std::filesystem::create_directories(root); }
    ~Files() { std::error_code ec; std::filesystem::remove_all(root,ec); }
    std::string Path(const char* name) const { return (root/name).string(); }
    void Text(const char* name, const char* data) const { std::ofstream(Path(name)) << data; }
};
using namespace LuxEngine;
void Core() {
    static_assert(!std::is_move_constructible_v<Window>);
    static_assert(!std::is_move_constructible_v<GpuDevice>);
    Timer timer;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    Check(timer.ElapsedSeconds() > 0,"timer");
    Check(Near(Vec3(1,0,0).Cross({0,1,0}).z,1),"cross product");
    WindowCloseEvent event; EventDispatcher dispatcher(event);
    dispatcher.Dispatch<WindowCloseEvent>([](auto&) { return true; });
    dispatcher.Dispatch<WindowCloseEvent>([](auto&) { return false; });
    Check(event.Handled,"handled event cannot be cleared");
    OrthographicCamera camera(-1,1,-1,1);
    camera.SetPosition({2,3,0}); camera.SetRotation(0.7f);
    auto identity = camera.GetViewMatrix() * (Mat4::Translate({2,3,0})*Mat4::RotateZ(0.7f));
    for (int i=0;i<16;++i) Check(Near(identity.elements[i],i%5==0?1.0f:0.0f),"camera inverse");
    TransformComponent transform; transform.rotation.x = ToRadians(90);
    Check(Near(transform.GetTransform().elements[6],1),"X rotation");
    transform.rotation = {0,ToRadians(90),0};
    Check(Near(transform.GetTransform().elements[2],-1),"Y rotation");
    AnimationClip clip; clip.duration=2; clip.loop=false;
    clip.AddKeyframe({1,{10,0,0}}); clip.AddKeyframe({0,{0,0,0}});
    Check(Near(clip.Sample(0.5f).position.x,5),"animation sorted interpolation");
    Check(Near(clip.Sample(1.5f).position.x,10),"animation last frame held");
    clip.AddKeyframe({1,{20,0,0}});
    Check(clip.keyframes.size()==2 && Near(clip.Sample(1).position.x,20),"duplicate animation time");
    clip.loop=true;
    Check(Near(clip.Sample(-0.5f).position.x,20),"negative animation time");
    Throws([&] { clip.AddKeyframe({-1}); },"invalid keyframe rejected");
    PhysicsEngine::Init();
    Vec3 p{0,0.5f,0}; Rigidbody2DComponent body; BoxCollider2DComponent collider;
    PhysicsEngine::Step2D(p,body,collider,1.0/60);
    Check(body.isGrounded && Near(p.y,0.5f),"ground contact");
    body.velocity.y=2; PhysicsEngine::Step2D(p,body,collider,1.0/60);
    Check(p.y>0.5f && !body.isGrounded,"jump leaves ground");
    p={0,0,0}; body={}; collider.isTrigger=true;
    PhysicsEngine::Step2D(p,body,collider,1.0/60);
    Check(p.y<0 && !body.isGrounded,"trigger does not block");
    body.type=BodyType::Kinematic; body.velocity={3,0}; p={0,5,0};
    PhysicsEngine::Step2D(p,body,collider,0.1);
    Check(Near(p.x,0.3f) && Near(p.y,5),"kinematic motion");
    const auto before=p; PhysicsEngine::Step2D(p,body,collider,-1);
    Check(Near(p.x,before.x),"invalid timestep ignored");
    Check(!Input::IsMouseButtonPressed(0) && !Input::IsMouseButtonPressed(33),"invalid mouse buttons");
}
void Scenes() {
    Scene scene; auto a=scene.CreateEntity("a"), b=scene.CreateEntity("b");
    struct Custom { int value=7; };
    a.AddComponent<Custom>(); a.AddComponent<Rigidbody2DComponent>();
    scene.DestroyEntity(a);
    Check(!a.IsValid() && !a.HasComponent<Custom>() && !a.HasComponent<Rigidbody2DComponent>(),"destroy all components");
    scene.DestroyEntity(a); Check(scene.GetEntityCount()==1 && b.IsValid(),"double destruction");
    Scene foreign; auto wrong=foreign.CreateEntity(); scene.DestroyEntity(wrong);
    Check(b.HasComponent<TagComponent>() && scene.GetEntityCount()==1,"foreign scene protected");
    const Scene& view=scene;
    Throws([&] { view.GetComponent<SpriteRendererComponent>(b.GetId()); },"const missing component throws");
    Check(!b.HasComponent<SpriteRendererComponent>(),"query never inserts");
    Throws([&] { a.GetComponent<TagComponent>(); },"stale entity throws");
    Throws([&] { b.AddComponent<TagComponent>(); },"duplicate component throws");
    Entity orphan;
    { Scene shortLived; orphan=shortLived.CreateEntity(); }
    Check(!orphan.IsValid() && !orphan.HasComponent<TagComponent>(),"scene lifetime tracked");
    Files files;
    auto persistent=scene.CreateEntity("spaces and \"quotes\"");
    persistent.GetComponent<TransformComponent>().position={1.25f,2,3};
    persistent.AddComponent<Rigidbody2DComponent>().velocity={4,5};
    persistent.AddComponent<BoxCollider3DComponent>();
    persistent.AddComponent<SpriteRendererComponent>().color={0.2f,0.4f,0.6f,1};
    Check(scene.Serialize(files.Path("scene.lux")),"serialize");
    Check(scene.Deserialize(files.Path("scene.lux")),"deserialize");
    Check(!persistent.IsValid() && !b.IsValid() && scene.GetEntityCount()==2,"load invalidates old handles");
    auto restored=scene.GetEntities();
    Check(restored[1].GetComponent<TagComponent>().tag=="spaces and \"quotes\"", "loaded name");
    Check(Near(restored[1].GetComponent<TransformComponent>().position.x,1.25f),"loaded transform");
    Check(Near(restored[1].GetComponent<Rigidbody2DComponent>().velocity.x,4),"loaded physics");
    Check(scene.Serialize(files.Path("roundtrip.lux")),"reserialize");
    auto bytes=[](const std::string& path) { std::ifstream in(path); return std::string(std::istreambuf_iterator<char>(in),{}); };
    Check(bytes(files.Path("scene.lux"))==bytes(files.Path("roundtrip.lux")),"component roundtrip");
    files.Text("bad.lux","LuxEngineScene 1 1\n1 \"partial\"\n");
    Check(!scene.Deserialize(files.Path("bad.lux")) && scene.GetEntityCount()==2,"failed load is transactional");
    auto custom=scene.CreateEntity(); custom.AddComponent<Custom>();
    Check(!scene.Serialize(files.Path("unsupported.lux")),"custom components never silently discarded");
}
void Assets() {
    double brightness[2]{};
    for (int variant=0;variant<2;++variant) {
        const auto icon=Branding::WindowIcon(variant==0);
        int width=0,height=0,channels=0;
        auto* pixels=stbi_load_from_memory(icon.data(),static_cast<int>(icon.size()),&width,&height,&channels,4);
        Check(pixels && width>0 && height>0,"embedded logo PNG decodes");
        double total=0,weight=0;
        for (int i=0;i<width*height;++i) {
            const double alpha=pixels[i*4+3]/255.0;
            total+=(pixels[i*4]+pixels[i*4+1]+pixels[i*4+2])*alpha;
            weight+=alpha*3;
        }
        stbi_image_free(pixels);
        Check(weight>0,"logo contains visible pixels");
        brightness[variant]=total/weight;
    }
    Check(brightness[0]>brightness[1],"dark theme selects brighter logo");
    for (bool dark : {false, true}) {
        auto* icon = Branding::CreateWindowIcon(dark);
        Check(icon && icon->w==32 && icon->h==32,"window icon has native base size");
        Check(SDL_SurfaceHasAlternateImages(icon),"window icon includes high DPI sizes");
        int left=32,top=32,right=-1,bottom=-1;
        for (int y=0;y<32;++y) for (int x=0;x<32;++x) {
            const auto* pixel=static_cast<const uint8*>(icon->pixels)+y*icon->pitch+x*4;
            if (pixel[3]>32) { left=std::min(left,x); top=std::min(top,y); right=std::max(right,x); bottom=std::max(bottom,y); }
        }
        Check(std::max(right-left+1,bottom-top+1)>=28,"logo fills icon area after transparent margin removal");
        SDL_DestroySurface(icon);
    }
    Files files; RawMeshData mesh;
    files.Text("quad.obj","v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf -4 -3 -2 -1\n");
    Check(AssetManager::LoadMesh(files.Path("quad.obj"),mesh),"OBJ load");
    Check(mesh.vertices.size()==12 && mesh.indices.size()==6,"OBJ triangulation");
    files.Text("bad.obj","v 0 0 0\nf 0 1 2\n");
    Check(!AssetManager::LoadMesh(files.Path("bad.obj"),mesh) && mesh.vertices.empty(),"invalid mesh fails");
    Check(!AssetManager::LoadMesh(files.Path("missing.obj"),mesh),"missing mesh fails");
    Check(!AssetManager::LoadTexture(files.Path("missing.png")),"missing device fails");
    Check(!AssetManager::HasTexture(files.Path("missing.png")),"failed image not cached");
    std::vector<uint8> bytes{1,2}; files.Text("empty","");
    Check(!AssetManager::LoadShaderBytes(files.Path("empty"),bytes) && bytes.empty(),"empty shader fails");
    files.Text("shader","abc");
    Check(AssetManager::LoadShaderBytes(files.Path("shader"),bytes) && bytes.size()==3,"shader load");
    AssetManager::Shutdown();
}
void Wav(const std::string& file) {
    std::ofstream out(file,std::ios::binary);
    auto u16=[&](uint16 value) { for(int i=0;i<2;++i) out.put(static_cast<char>((value>>(i*8))&255)); };
    auto u32=[&](uint32 value) { for(int i=0;i<4;++i) out.put(static_cast<char>((value>>(i*8))&255)); };
    out.write("RIFF",4); u32(36+16000); out.write("WAVEfmt ",8); u32(16); u16(1); u16(1); u32(8000); u32(16000); u16(2); u16(16); out.write("data",4); u32(16000);
    for(int i=0;i<8000;++i) u16(static_cast<uint16>(static_cast<int16>(std::sin(i*0.2)*1000)));
}
void Audio() {
    Files files; Wav(files.Path("tone.wav"));
    Check(!AudioEngine::PlayMusic(files.Path("tone.wav")),"uninitialized audio fails");
    Check(AudioEngine::Init() && AudioEngine::Init(),"audio initialization idempotent");
    Check(!AudioEngine::PlayMusic(files.Path("missing.wav")),"missing audio fails");
    Check(!AudioEngine::IsMusicPlaying(),"missing audio never plays");
    AudioEngine::SetMasterVolume(2); Check(Near(AudioEngine::GetMasterVolume(),1),"volume clamp");
    Check(AudioEngine::PlayMusic(files.Path("tone.wav"),true,0.1f),"WAV playback");
    Check(AudioEngine::IsMusicPlaying(),"music is playing");
    Check(AudioEngine::PlaySound(files.Path("tone.wav"),0.1f),"sound playback");
    AudioEngine::StopMusic(); Check(!AudioEngine::IsMusicPlaying(),"stop music");
    Check(AudioEngine::PlayMusic(files.Path("tone.wav"),false),"non-loop playback");
    std::this_thread::sleep_for(std::chrono::milliseconds(1400));
    Check(!AudioEngine::IsMusicPlaying(),"music ends naturally");
    AudioEngine::Shutdown(); AudioEngine::Shutdown();
    Check(!AudioEngine::IsMusicPlaying(),"audio shutdown");
}
struct Graphics {
    Window window; GpuDevice gpu;
    Graphics() {
        if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        WindowProps props; props.width=320; props.height=240; props.title="LuxEngine GPU verification";
        Check(window.Initialize(props),"GPU test window");
        Check(window.HasWindowIcon(),"embedded window icon applied");
        const auto expected = SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK ? ColorTheme::Dark : ColorTheme::Light;
        Check(window.GetColorTheme()==expected,"window icon follows system theme");
        SDL_Event changed{}; changed.type=SDL_EVENT_SYSTEM_THEME_CHANGED;
        Check(SDL_PushEvent(&changed),"theme event queued");
        window.PollEvents();
        Check(window.HasWindowIcon() && window.GetColorTheme()==expected,"theme event preserves correct icon");
        Check(gpu.Initialize(window.GetNativeHandle()),SDL_GetError());
    }
    ~Graphics() { AssetManager::Shutdown(); Renderer2D::Shutdown(); Renderer3D::Shutdown(); gpu.Shutdown(); window.Shutdown(); SDL_Quit(); }
};
void Lifecycle() {
    struct Probe : Application {
        int shutdowns=0;
        bool failInit=false, failRender=false;
        void OnInit() override { if (failInit) throw std::runtime_error("init failure"); }
        void OnUpdate(double) override { if (!failRender) Close(); }
        void OnRender(float) override { if (failRender) throw std::runtime_error("render failure"); }
        void OnShutdown() override { ++shutdowns; }
    };
    {
        Probe app;
        Check(app.Initialize(),"lifecycle init");
        Check(app.Run()==0 && app.shutdowns==1,"Run dispatches derived shutdown");
        app.Shutdown(); Check(app.shutdowns==1,"shutdown idempotent");
    }
    {
        Probe app; app.failInit=true;
        Throws([&] { app.Initialize(); },"init failure propagated");
        Check(app.shutdowns==1,"partial initialization cleaned up");
    }
    {
        Probe app; app.failRender=true; Check(app.Initialize(),"render failure init");
        Throws([&] { app.Run(); },"render failure propagated");
        Check(app.shutdowns==1,"render exception cleanup");
    }
}
void Gpu() {
    Graphics graphics;
    Files files;
    auto* surface=SDL_CreateSurface(2,2,SDL_PIXELFORMAT_RGBA32);
    Check(surface!=nullptr,"test image surface");
    SDL_FillSurfaceRect(surface,nullptr,SDL_MapSurfaceRGBA(surface,255,0,0,255));
    Check(SDL_SaveBMP(surface,files.Path("red.bmp").c_str()),"write test image");
    SDL_DestroySurface(surface);
    auto texture=AssetManager::LoadTexture(files.Path("red.bmp"),graphics.gpu.GetHandle());
    Check(texture && texture->IsValid() && texture->GetWidth()==2,"real image upload");
    Check(!AssetManager::LoadTexture(files.Path("absent.png"),graphics.gpu.GetHandle()),"missing GPU image fails");
    Renderer2D::Init(&graphics.gpu); Renderer3D::Init(&graphics.gpu);
    OrthographicCamera camera(-2,2,-2,2); PerspectiveCamera perspective(ToRadians(60),4.0f/3,0.1f,100);
    for(int frame=0;frame<3;++frame) {
        graphics.window.PollEvents();
        Check(graphics.gpu.BeginFrame(),"begin frame");
        graphics.gpu.Clear(0,0,0,1);
        Renderer2D::BeginScene(camera);
        Renderer2D::DrawSprite({-1,0},{1,1},texture.get());
        Renderer2D::DrawQuad(Vec2(0,0),{1,1},{0,1,0,1});
        Renderer2D::EndScene();
        Check(Renderer2D::GetStats().drawCalls==2 && Renderer2D::GetStats().quadCount==2,"real 2D draw calls");
        Renderer3D::BeginScene(perspective,{});
        Renderer3D::DrawCube({1,0,0},{1,1,1},{});
        Renderer3D::EndScene();
        Check(Renderer3D::GetStats().drawCalls==1 && Renderer3D::GetStats().meshCount==1,"real 3D draw call");
        graphics.gpu.EndFrame();
    }
    Check(SDL_WaitForGPUIdle(graphics.gpu.GetHandle()),"GPU completion");
    texture.reset(); AssetManager::Shutdown();
    GpuBuffer buffer; Check(buffer.Initialize(graphics.gpu.GetHandle(),BufferType::Vertex,4),"buffer init");
    const uint32 value=42;
    Check(!buffer.SetData(&value,8),"oversized upload rejected");
    Check(buffer.SetData(&value,4),"buffer upload");
    GpuBuffer moved=std::move(buffer); Check(!buffer.IsValid() && moved.IsValid(),"buffer ownership transfer");
    moved.Release();
    const uint32 pixel=0xffffffff;
    GpuTexture delayed;
    Check(delayed.Initialize(graphics.gpu.GetHandle(),1,1,&pixel),"delayed texture init");
    graphics.gpu.Shutdown();
    Check(!delayed.IsValid() && !delayed.GetHandle(),"device shutdown invalidates resources");
    delayed.Release();
}
}
int main(int argc, char** argv) {
    try {
        const std::string_view suite=argc>1?argv[1]:"core";
        if(suite=="core") Core(); else if(suite=="scene") Scenes(); else if(suite=="assets") Assets(); else if(suite=="audio") Audio(); else if(suite=="gpu") Gpu(); else if(suite=="lifecycle") Lifecycle(); else throw std::runtime_error("Unknown suite");
        std::cout << suite << ": " << checks << " checks passed\n";
        return 0;
    } catch(const std::exception& error) { std::cerr << "FAILED: " << error.what() << '\n'; return 1; }
}

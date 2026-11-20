void RoofSceneResources(const std::filesystem::path& game) {
    // Static source-period fixtures. All three use the same lighting time and
    // ordinary wide camera, so image differences can be attributed to the
    // independently registered MODEL01.DVS and matching DAK/GIM package.
    constexpr float near_z = 100.f, far_z = 10400.f;
    const float fy = 1.f / std::tan(30.f * 3.14159265358979323846f / 360.f);
    const float fx = fy * 272.f / 480.f;
    const float z_scale = (far_z + near_z) / (far_z - near_z);
    const float z_shift = -2.f * far_z * near_z / (far_z - near_z);
    const std::array<float,16> camera{fx,0,0,0, 0,0,z_scale,1, 0,fy,0,0,
        0,-980.f*fy,4400.f*z_scale+z_shift,4400.f};
    constexpr unsigned periods[]{0,2,3};
    std::set<std::uint64_t> scene_images;
    for (unsigned variant=0; variant<3; ++variant) {
        dokodemo::gameplay::RoofScene roof;
        dokodemo::recovered::RngNativeOwner rng;
        rng.ApplyInitializerTimeT32(168,1,"Controlled roof component input");
        std::string error;
        const bool loaded = roof.Load(game/"assets/game/post-profile",periods[variant],rng,error);
        Require(loaded, error.c_str());
        Require(roof.resource()==66+variant,"Wrong source roof period registration");
        const auto& spawn = roof.Placement();
        Require(spawn[0]==0.f && spawn[1]==0.f && spawn[2]==0.f &&
                std::abs(spawn[3]-3.14159274f) < .00001f,"Incorrect actor BD28 roof placement");
        const auto& mesh = roof.Background();
        Require(mesh.ContainsActor({spawn[0],spawn[1]}) && !mesh.ContainsActor({0,2000}),
                "Rooftop walking region did not constrain the actor");
        const auto outer = mesh.QueryActorRegion({0,2000});
        Require(!outer.inside && std::abs(outer.target[0]-62.822f)<.01f &&
                std::abs(outer.target[1]-90.f)<.01f,"Wrong authored roof region center");
        std::vector<std::uint8_t> vram(surface*2+surface/2);
        dokodemo::gameplay::RoomLighting lighting;
        bool changed = false;
        Require(roof.Draw(camera,0,12*216000,vram,0,lighting,&changed) && changed,
                "Roof main geometry failed to touch the clear surface");
        const auto image = Visible(vram,0);
        Require(image.visible > 0 && lighting.points.size() > 0,
                "Missing real rooftop lighting or geometry");
        scene_images.insert(image.hash);
        std::fill(vram.begin(),vram.end(),0);
        Require(roof.Draw(camera,0,12*216000+1,vram,surface,lighting,&changed) && changed,
                "Could not advance roof tracks on the other display buffer");
        const auto& clips = roof.main_clips();
        const unsigned count = variant==1 ? 3 : 4;   // Source 52AC, 606C.
        Require(clips.size()>count && clips.back().first==0 &&
                std::all_of(clips.begin(),clips.begin()+count,[](const auto& c){return c.second==160;}),
                "Initial authored rooftop motions did not advance");
        std::cout<<"Roof source resource "<<roof.resource()<<" visible "<<image.visible
                 <<" framebuffer hash "<<image.hash<<"\n";
    }
    Require(scene_images.size()==3,"The three registered rooftop scenes drew the same image");
}

int CheckDestinationResources(const std::filesystem::path& game) {
    using namespace dokodemo::gameplay;
    constexpr float near_z=100.f,far_z=10400.f;
    const float fy=1.f/std::tan(30.f*3.14159265358979323846f/360.f),fx=fy*272.f/480.f;
    const float z_scale=(far_z+near_z)/(far_z-near_z),z_shift=-2.f*far_z*near_z/(far_z-near_z);
    const std::array<float,16> camera{fx,0,0,0, 0,0,z_scale,1, 0,fy,0,0,
        0,-980.f*fy,4400.f*z_scale+z_shift,4400.f};
    constexpr std::array<unsigned,3> periods{0,2,3};
    unsigned count=0;
    std::set<std::uint64_t> images;
    const auto media=game/"assets/game/post-profile";
    for (unsigned scene=1;scene<20;++scene) {
        if (!OutingScene::Supports(scene)) continue;
        for (unsigned variant=0;variant<periods.size();++variant) {
            const unsigned resource=30+3*scene+variant;
            OutingScene model;
            std::string error;
            if (!model.Load(media,scene,periods[variant],error))
                throw std::runtime_error("Destination "+std::to_string(resource)+": "+error);
            Require(model.resource()==resource,"Incorrect destination period variant");
            std::vector<std::uint8_t> vram(surface*2+surface/2);
            RoomLighting lighting;
            const float light_times[]{0,12,18};
            for (const std::size_t offset : {std::size_t{0},std::size_t{surface}}) {
                bool changed=false;
                const bool drawn=model.Draw(camera,0,std::uint32_t(light_times[variant]*216000),vram,offset,lighting,&changed);
                const auto image=Visible(vram,offset);
                if (!drawn || !changed || !image.visible)
                    throw std::runtime_error("No destination geometry for resource "+std::to_string(resource));
                std::cout << "Scene " << scene << " resource " << resource << " buffer " << offset
                          << " RGB pixels " << image.visible << " hash " << image.hash << '\n';
                if (!offset) images.insert(image.hash);
                std::fill(vram.begin(),vram.end(),0);
            }
            ++count;
        }
    }
    Require(count==54 && images.size()>periods.size(),"Missing destination registrations or distinct draws");
    std::cout << count << " destination registrations, " << images.size() << " distinct starting images\n";
    return 0;
}

#include "experiment/infrastructure/CameraSequence.h"
#include "experiment/infrastructure/CameraRecipe.h"
#include "app/ExperimentCameraSession.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <stdexcept>

namespace Data = ParallelRoam::Experiment::Infrastructure;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main()
{
    const auto directory = std::filesystem::temp_directory_path() /
        ("roam-camera-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try
    {
        Data::CameraSequence frames;
        for (std::uint32_t i=0;i<4;++i) frames.push_back(Data::MakeCameraFrame({.1F,3.1234567F,5.6F},{0,0,0},i));
        std::filesystem::create_directories(directory);
        Data::SaveCameraSequence(directory/"route.csv",frames);
        const auto restored = Data::LoadCameraSequence(directory/"route.csv");
        Check(restored.size()==frames.size(),"roundtrip size");
        for (std::size_t i=0;i<frames.size();++i)
            Check(restored[i].NoHash==frames[i].NoHash && restored[i].ZoHash==frames[i].ZoHash,"float roundtrip");
        auto corrupt = frames; corrupt[1].Position.x += .25F;
        bool rejected=false;
        try { Data::ValidateCameraSequence(corrupt); } catch (...) { rejected=true; }
        Check(rejected,"tampered pose accepted");
        std::ifstream original(directory/"route.csv");
        std::string header; std::getline(original, header);
        header.replace(header.find(",px,"), 4, ",py,");
        std::ofstream altered(directory/"wrong-header.csv");
        altered << header << '\n' << original.rdbuf(); altered.close();
        rejected=false;
        try { (void)Data::LoadCameraSequence(directory/"wrong-header.csv"); } catch (...) { rejected=true; }
        Check(rejected,"renamed middle column accepted");
        ParallelRoam::App::ExperimentCameraSession session;
        session.Load(directory/"route.csv"); session.Play();
        Check(session.TakeReset() && !session.TakeReset(),"reset lifecycle");
        Check(session.Current()->Index==0,"first frame");
        session.CompleteOpportunity(); session.Pause();
        Check(session.Current()->Index==0 && !session.ShouldUpdate(),"pause advanced pose");
        session.Step(); Check(session.Current()->Index==1,"step");
        Check(!session.TakeReset(),"step reset");
        session.CompleteOpportunity(); session.Play();
        Check(session.Current()->Index==2,"resume");
        session.CompleteOpportunity(); Check(session.Current()->Index==3,"tail");
        session.CompleteOpportunity(); Check(!session.ShouldUpdate(),"tail not paused");
        session.Restart(); Check(session.TakeReset() && session.Current()->Index==0,"restart");
        session.Capture({0,4,5},{0,-.3F,-1},2);
        session.Capture({1,4,5},{0,-.3F,-1},3);
        Check(session.KeyCount()==2,"record");
        session.RemoveLast(); Check(session.KeyCount()==1,"remove");
        std::filesystem::remove_all(directory);
        std::cout << "camera roundtrip, corruption, pause, step, restart: ok\n";
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        std::filesystem::remove_all(directory);
        return 1;
    }
}

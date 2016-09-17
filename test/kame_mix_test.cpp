#include <audio_system.h>
#include <sound.h>
#include <stream.h>
#include <thread>
#include <chrono>
#include <iostream>
#include <fstream>
#include <string>

using namespace KameMix;
using std::cout;
using std::string;

void onSoundFinished(Sound *sound, void *udata)
{
  cout << "sound finished\n";
}

void onStreamFinished(Stream *stream, void *udata)
{
  cout << "stream finished\n";
}

string soundPath()
{
  std::ifstream fin("sound_path.txt");
  string path;
  if (fin.is_open()) {
    std::getline(fin, path);
  } else {
    path = "sound/";
  }
  return path;
}

int main(int argc, char *argv[])
{
  if (!AudioSystem::init()) {
    cout << "AudioSystem::init failed\n";
    return 1;
  }

  cout << "Initialized KameMix\n";

  AudioSystem::setSoundFinished(onSoundFinished, nullptr);
  AudioSystem::setStreamFinished(onStreamFinished, nullptr);

  //Group effect_group(0.25f);
  //Listener listener(0, 0);

  string sound_path = soundPath();

  string spell1_path = sound_path + "spell1_0.wav";
  Sound spell1(spell1_path.c_str());
  if (!spell1.isLoaded()) {
    cout << "Couldn't load " << spell1_path << "n";
    return EXIT_FAILURE;
  }

  string spell3_path = sound_path + "spell3.wav";
  Sound spell3(spell3_path.c_str());
  if (!spell3.isLoaded()) {
    cout << "Couldn't load " << spell3_path << "n";
    return EXIT_FAILURE;
  }

  string cow_path = sound_path + "Mudchute_cow_1.ogg";
  Sound cow(cow_path.c_str());
  if (!cow.isLoaded()) {
    cout << "Couldn't load " << cow_path << "n";
    return EXIT_FAILURE;
  }

  string duck_path = sound_path + "Mudchute_duck_2.ogg";
  Stream duck(duck_path.c_str());
  if (!duck.isLoaded()) {
    cout << "Couldn't load " << duck_path << "n";
    return EXIT_FAILURE;
  }

  string music1_path = sound_path + "dark fallout.ogg";
  Sound music1(music1_path.c_str());
  if (!music1.isLoaded()) {
    cout << "Couldn't load " << music1_path << "n";
    return EXIT_FAILURE;
  }

  string music2_path = sound_path + "a new beginning.ogg";
  Stream music2(music2_path.c_str());
  if (!music2.isLoaded()) {
    cout << "Couldn't load " << music2_path << "n";
    return EXIT_FAILURE;
  }

  spell1.play(5);
  cout << "play spell1 5 times\n";
  spell3.play(5);
  cout << "play spell3 5 times\n";
  cow.play(5);
  cout << "play cow 5 times\n";
  duck.play(5);
  cout << "play duck 5 times\n";

  double time_ms = 0.0;
  int count = 0;

  while (true) {
    AudioSystem::update();
    std::this_thread::sleep_for(std::chrono::milliseconds(17));
    time_ms += 17;

    if (time_ms > 5000 && count == 0) {
      cout << "play music1\n";
      time_ms = 0.0;
      count++;
      music1.play(0, false);
    } else if (time_ms > 10000 && count == 1) {
      cout << "Fadeout music1 over 10 secs\n";
      time_ms = 0.0;
      count++;
      music1.fadeout(10.0f);
    } else if (time_ms > 10000 && count == 2) {
      cout << "Fadein music2 over 10 secs, and play 15 secs total\n";
      time_ms = 0.0;
      count++;
      music2.fadein(0, 10.0f, false);
    } else if (time_ms > 15000 && count == 3) {
      cout << "stop music2\n";
      time_ms = 0.0;
      count++;
      music2.stop();
    } else if (count == 4) {
      if (AudioSystem::numberPlaying() == 0) {
        cout << "Test complete\n";
        break;
      }
    }
  }

  AudioSystem::shutdown();
  cout << "Shutdown KameMix\n";

  return 0;
}

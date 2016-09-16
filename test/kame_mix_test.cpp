#include <audio_system.h>
#include <sound.h>
#include <stream.h>
#include <thread>
#include <chrono>
#include <iostream>

using namespace KameMix;
using std::cout;

void onSoundFinished(Sound *sound, void *udata)
{
  cout << "sound finished\n";
}

void onStreamFinished(Stream *stream, void *udata)
{
  cout << "stream finished\n";
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

  Sound spell1("sound/spell1_0.wav");
  if (!spell1.isLoaded()) {
    cout << "Couldn't load 'spell1_0.wav'\n";
  }

  Sound spell3("sound/spell3.wav");
  if (!spell3.isLoaded()) {
    cout << "Couldn't load 'spell3.wav'\n";
  }

  Sound cow("sound/Mudchute_cow_1.ogg");
  if (!cow.isLoaded()) {
    cout << "Couldn't load 'Mudchute_cow_1.ogg'\n";
  }

  Stream duck("sound/Mudchute_duck_2.ogg");
  if (!duck.isLoaded()) {
    cout << "Couldn't load 'Mudchute_duck_2.ogg'\n";
  }

  Sound music1("sound/dark fallout.ogg");
  if (!music1.isLoaded()) {
    cout << "Couldn't load 'a new beginning.ogg'\n";
  }

  Stream music2("sound/a new beginning.ogg");
  if (!music2.isLoaded()) {
    cout << "Couldn't load 'dark fallout.ogg'\n";
  }

  spell1.play(5);
  cout << "play spell1 5 times\n";
  spell3.play(5);
  cout << "play spell3 5 times\n";
  cow.play(5);
  cout << "play cow 5 times\n";
  duck.play(5);
  cout << "play duck 5 times\n";

  //AudioSystem::setMasterVolume(.5f);

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

#include <system.h>
#include <sound.h>
#include <stream.h>
#include <cmath>
#include <thread>
#include <chrono>
#include <iostream>

using namespace KameMix;
using std::cout;
using std::cerr;

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
  //OutAudioFormat format = OutFormat_S16;
  OutAudioFormat format = OutFormat_Float;
  if (!System::init(44100, 2048, format)) {
    cout << "System::init failed\n";
    return 1;
  }

  cout << "Initialized KameMix\n";

  System::setSoundFinished(onSoundFinished, nullptr);
  System::setStreamFinished(onStreamFinished, nullptr);

  //Group effect_group(0.25f);
  //Listener listener(0, 0);

  const char *file_path = "sound/spell1.wav";
  Stream spell1(file_path);
  if (!spell1.isLoaded()) {
    cerr << "Couldn't load " << file_path << "\n";
    return EXIT_FAILURE;
  }

  file_path = "sound/spell3.wav";
  Sound spell3(file_path);
  if (!spell3.isLoaded()) {
    cerr << "Couldn't load " << file_path << "\n";
    return EXIT_FAILURE;
  }

  file_path = "sound/cow.ogg";
  Sound cow(file_path);
  if (!cow.isLoaded()) {
    cerr << "Couldn't load " << file_path << "\n";
    return EXIT_FAILURE;
  }

  file_path = "sound/duck.ogg";
  Stream duck(file_path);
  if (!duck.isLoaded()) {
    cerr << "Couldn't load " << file_path << "\n";
    return EXIT_FAILURE;
  }

  file_path = "sound/dark fallout.ogg";
  Stream music1(file_path);
  if (!music1.isLoaded()) {
    cerr << "Couldn't load " << file_path << "\n";
    return EXIT_FAILURE;
  }

  file_path = "sound/a new beginning.ogg";
  Sound music2(file_path);
  if (!music2.isLoaded()) {
    cerr << "Couldn't load " << file_path << "\n";
    return EXIT_FAILURE;
  }

  spell1.play(6);
  cout << "play spell1 7 times\n";
  spell3.play(6);
  cout << "play spell3 7 times\n";
  cow.play(6);
  cout << "play cow 7 times\n";
  duck.play(6);
  duck.setVolume(1.25f);
  cout << "play duck 7 times\n";

  double time_ms = 0.0;
  int count = 0;
  const int frame_ms = 17;
  const double frames_per_sec = 1000.0/frame_ms;
  float listener_x = .5f;
  float listener_y = .5f;
  System::setListenerPos(listener_x, listener_y);
  float x = listener_x;
  float y = .75f;
  bool going_left = true;

  while (true) {
    System::update();
    std::this_thread::sleep_for(std::chrono::milliseconds(frame_ms));
    time_ms += frame_ms;

    if (time_ms > 10000 && count == 0) {
      cout << "play music1 starting at 40sec for 10secs\n";
      time_ms = 0.0;
      count++;
      music1.playAt(40);
    } else if (time_ms > 10000 && count == 1) {
      cout << "Fadeout music1 over 10 secs\n";
      time_ms = 0.0;
      count++;
      music1.fadeout(10);
    } else if (time_ms > 10000 && count == 2) {
      cout << "Fadein music2 over 10 secs, pause after 5 secs\n";
      time_ms = 0.0;
      count++;
      music2.fadein(10.0);
    } else if (time_ms > 5000 && count == 3) {
      cout << "pause music2 for 3 secs\n";
      time_ms = 0.0;
      count++;
      music2.pause();
    } else if (time_ms > 3000 && count == 4) {
      cout << "unpase music2, and continue fadein over 5 secs\n";
      time_ms = 0.0;
      count++;
      music2.unpause();
    } else if (time_ms > 5000 && count == 5) {
      cout << "fadein complete, play for 5 secs and then stop\n";
      time_ms = 0.0;
      count++;
    } else if (time_ms > 5000 && count == 6) {
      cout << "stop music2\n";
      time_ms = 0.0;
      count++;
      music2.stop();
      cout << "play spell1 moving left to right and back for 15secs\n";
      spell1.setMaxDistance(1.0f);    
      spell1.play(-1);
    } else if (count == 7) {
      spell1.setPos(x, y);
      // 2 cycles in 10 secs
      float dx = (float)(4*spell1.getMaxDistance() / 10 / frames_per_sec);
      if (going_left) {
        x -= dx;
      } else {
        x += dx;
      }
      if (std::abs(listener_x - x) >= spell1.getMaxDistance()) {
        going_left = !going_left;
      }
      if (time_ms > 15000) {
        spell1.stop();
        count++;
      }
    } else if (count == 8) {
      if (System::numberPlaying() == 0) {
        cout << "Test complete\n";
        break;
      }
    }
  }

  System::shutdown();
  cout << "Shutdown KameMix\n";

  return 0;
}

#include <KameMix.h>
#include <cmath>
#include <cassert>
#include <thread>
#include <chrono>
#include <iostream>

using namespace KameMix;
using std::cout;
using std::cerr;

void onSoundFinished(Sound *sound, void *udata)
{
  cout << "sound finished\n";
  assert(!sound->isPlaying());
}

void onStreamFinished(Stream *stream, void *udata)
{
  cout << "stream finished\n";
  assert(!stream->isPlaying());
}

Sound spell1;
Sound spell3;
Sound cow;
Stream duck;
Stream music1;
Sound music2;

const int frames_per_sec = 60;
const double frame_ms = 1000.0 / frames_per_sec;

double update_ms(double msec);
bool loadAudio();
void test1();
void test2();
void test3();
void test4();
void test5();

int main(int argc, char *argv[])
{
  //OutAudioFormat format = OutFormat_S16;
  OutAudioFormat format = OutFormat_Float;
  if (!System::init(44100, 2048, format)) {
    cout << "System::init failed\n";
    return 1;
  }

  cout << "Initialized KameMix\n";

  if (!loadAudio()) {
    return EXIT_FAILURE;
  }

  System::setSoundFinished(onSoundFinished, nullptr);
  System::setStreamFinished(onStreamFinished, nullptr);

  assert(System::getMasterVolume() == 1.0f);
  System::setMasterVolume(.5f);
  assert(System::getMasterVolume() == .5f);
  System::setMasterVolume(1.0f);

  {
    float a, b;
    System::getListenerPos(a, b);
    assert(a == 0 && b == 0);

    System::setListenerPos(.5f, .75f);
    System::getListenerPos(a, b);
    assert(a == .5f && b == .75f);
  }

  int group1 = System::createGroup();
  System::setGroupVolume(group1, .75f);
  assert(System::getGroupVolume(group1) == .75f);

  assert(spell1.getGroup() == -1);
  spell1.setGroup(group1);
  assert(spell1.getGroup() == group1);

  duck.setVolume(1.5);
  assert(duck.getVolume() == 1.5);

  //test1();
  //test2();
  //test3();
  //test4();
  test5();

  cout << "Test complete\n";

  System::shutdown();
  cout << "Shutdown KameMix\n";

  return 0;
}

bool loadAudio()
{
  const char *file_path = "sound/spell1.wav";
  spell1.load(file_path);
  if (!spell1.isLoaded()) {
    cerr << "Couldn't load " << file_path << "\n";
    return false;
  }

  file_path = "sound/spell3.wav";
  spell3.load(file_path);
  if (!spell3.isLoaded()) {
    cerr << "Couldn't load " << file_path << "\n";
    return false;
  }

  file_path = "sound/cow.ogg";
  cow.load(file_path);
  if (!cow.isLoaded()) {
    cerr << "Couldn't load " << file_path << "\n";
    return false;
  }

  file_path = "sound/duck.ogg";
  duck.load(file_path);
  if (!duck.isLoaded()) {
    cerr << "Couldn't load " << file_path << "\n";
    return false;
  }

  file_path = "sound/dark fallout.ogg";
  music1.load(file_path);
  if (!music1.isLoaded()) {
    cerr << "Couldn't load " << file_path << "\n";
    return false;
  }

  file_path = "sound/a new beginning.ogg";
  music2.load(file_path);
  if (!music2.isLoaded()) {
    cerr << "Couldn't load " << file_path << "\n";
    return false;
  }

  return true;
}

double update_ms(double msec)
{
  const std::chrono::duration<double, std::milli> sleep_duration(frame_ms);
  double total_time = 0;
  while (true) {
    System::update();
    std::this_thread::sleep_for(sleep_duration);
    total_time += frame_ms;
    if (total_time >= msec) {
      break;
    }
  }
  return total_time;
}

void test1()
{
  cout << "play spell1 7 times\n";
  spell1.play(6);
  assert(spell1.isPlaying());

  cout << "play spell3 7 times\n";
  spell3.play(6);
  assert(spell3.isPlaying());

  cout << "play cow 7 times\n";
  cow.play(6);
  assert(cow.isPlaying());

  cout << "play duck 7 times\n";
  duck.play(6);
  assert(duck.isPlaying());

  while (true) {
    update_ms(1000);
    if (System::numberPlaying() == 0) {
      break;
    }
  }
}

void test2()
{
  cout << "Play music1 starting at 40sec for 10secs\n";
  music1.playAt(40);
  update_ms(10000);

  cout << "Fadeout music1 over 10 secs\n";
  music1.fadeout(10);
  update_ms(10000);

  cout << "Fadein music2 over 10 secs, pause after 5 secs\n";
  music2.fadein(10.0);
  update_ms(5000);

  cout << "Pause music2 for 3 secs\n";
  music2.pause();
  update_ms(3000);

  cout << "Unpause music2, and continue fadein over 5 secs\n";
  music2.unpause();
  update_ms(5000);

  cout << "Fadein complete, play for 5 secs and then stop\n";
  update_ms(5000);

  cout << "Stop music2\n";
  music2.stop();
}

void test3()
{
  float listener_x = .5f;
  float listener_y = .5f;
  System::setListenerPos(listener_x, listener_y);
  float x = listener_x;
  float y = listener_y + .25f;

  cout << "Play spell1 moving left to right and back for 20secs\n";
  spell1.setMaxDistance(1.0f);    
  spell1.setPos(x, y);
  spell1.play(-1);

  bool going_left = true;
  double total_time = 0;

  while (true) {
    // 2 cycle in 10 secs
    float dx = (float)(4*spell1.getMaxDistance() / 10 / frames_per_sec);
    if (going_left) {
      x -= dx;
    } else {
      x += dx;
    }

    if (std::abs(listener_x - x) >= spell1.getMaxDistance() * 1.05) {
      going_left = !going_left;
    }

    total_time += update_ms(frame_ms);
    if (total_time > 20000) {
      spell1.stop();
      break;
    }

    spell1.setPos(x, y);
  }
}

void test4()
{
  float listener_x = .5f;
  float listener_y = .5f;
  System::setListenerPos(listener_x, listener_y);
  float lx = listener_x - .5f;
  float rx = listener_x + .5f;
  float y = listener_y;

  cout << "Play duck for 12 secs, swapping sides every 3 secs\n";
  duck.setMaxDistance(1.0f);
  duck.setPos(lx, y);
  duck.play(-1);
  update_ms(3000);

  duck.setPos(rx, y);
  update_ms(3000);

  duck.setPos(lx, y);
  update_ms(3000);

  duck.setPos(rx, y);
  update_ms(3000);

  duck.setPos(lx, y);
  update_ms(3000);

  cout << "Stop duck\n";
  duck.stop();
}

void test5()
{
  cout << "Play music1 for 5 secs at 100% volume\n";
  music1.setVolume(1.0f);
  music1.setGroup(-1);
  music1.play(-1);
  update_ms(5000);

  cout << "Set music1 volume to 0% for 3 secs\n";
  music1.setVolume(0);
  update_ms(3000);

  cout << "Set music1 volume to 100% for 5 secs\n";
  music1.setVolume(1.0f);
  update_ms(5000);

  cout << "Set music1 volume to 25% for 5 secs\n";
  music1.setVolume(.25f);
  update_ms(5000);

  cout << "Set music1 volume to 100% for 5 secs\n";
  music1.setVolume(1.0f);
  update_ms(5000);

  cout << "Stop music1\n";
  music1.stop();
}
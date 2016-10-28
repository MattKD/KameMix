#include <KameMix.h>
#include <cmath>
#include <cassert>
#include <thread>
#include <chrono>
#include <iostream>

using namespace KameMix;
using std::cout;
using std::cerr;

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
void test6();
void test7();

int main(int argc, char *argv[])
{
  //OutputFormat format = OutputS16;
  OutputFormat format = OutputFloat;
  if (!System::init(44100, 2048, format)) {
    cout << "System::init failed\n";
    return 1;
  }

  cout << "Initialized KameMix\n";

  if (!loadAudio()) {
    return EXIT_FAILURE;
  }

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

  test1();
  test2();
  test3();
  test4();
  test5();
  test6();
  test7();

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
  cout << "Test1: Tests playing multiple sounds/streams at once\n";

  cout << "Play music1\n";
  music2.play();

  cout << "Play spell1 7 times\n";
  spell1.play(6);
  assert(spell1.isPlaying());

  cout << "Play spell3 7 times\n";
  spell3.play(6);
  assert(spell3.isPlaying());

  cout << "Play cow 7 times\n";
  cow.play(6);
  assert(cow.isPlaying());

  cout << "Play duck 7 times\n";
  duck.play(6);
  assert(duck.isPlaying());

  while (true) {
    update_ms(1000);
    if (System::numberPlaying() == 1) {
      cout << "Stop music2\n";
      music2.stop();
    } else if (System::numberPlaying() == 0) {
      break;
    }
  }

  cout << "Test1 complete\n";
}

void test2()
{
  cout << "Test2: Tests fading in/out, and pausing\n";

  cout << "Play music1 for 10secs with 5 second fadein\n";
  music1.fadein(5.0f);
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

  cout << "Test2 complete\n";
}

void test3()
{
  cout << "Test3: Tests changing 2d position of sound in small steps\n";

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
  cout << "Test3 complete\n";
}

void test4()
{
  cout << "Test4: Tests setting 2d position without small steps\n";

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

  cout << "Test4 complete\n";
}

void test5()
{
  cout << "Test5: Tests changing volume\n";

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
  while (music1.isPlaying()) {
    update_ms(frame_ms);
  }

  cout << "Test5 complete\n";
}

void test6()
{
  cout << "Test6: Tests changing stream time position\n";

  bool use_stop = false;
  for (int i = 0; i < 2; ++i) {
    if (use_stop) {
      cout << "Testing replaying stream at different times with waiting "
              "for stop.\n";
    } else {
      cout << "Testing replaying stream at different times without waiting "
              "for stop.\n";
    }

    cout << "Play music1 for 5 secs then skip to 20secs\n";
    music1.play();
    update_ms(5000);

    cout << "Continue playing at 20secs for 5 secs then skip to 40secs\n";
    if (use_stop) {
      music1.stop();
      while (music1.isPlaying()) {
        update_ms(frame_ms);
      }
    }
    music1.playAt(20.0);
    update_ms(5000);

    cout << "Continue playing at 40secs for 5 secs then skip to 60secs\n";
    if (use_stop) {
      music1.stop();
      while (music1.isPlaying()) {
        update_ms(frame_ms);
      }
    }
    music1.playAt(40.0);
    update_ms(5000);

    cout << "Continue playing at 60secs for 5 secs then skip to 80secs\n";
    if (use_stop) {
      music1.stop();
      while (music1.isPlaying()) {
        update_ms(frame_ms);
      }
    }
    music1.playAt(60.0);
    update_ms(5000);

    cout << "Continue playing at 80secs for 5 secs then stop\n";
    if (use_stop) {
      music1.stop();
      while (music1.isPlaying()) {
        update_ms(frame_ms);
      }
    }
    music1.playAt(80.0);
    update_ms(5000);

    cout << "Stop music1\n";
    music1.stop();
    while (music1.isPlaying()) {
      update_ms(frame_ms);
    }

    use_stop = true;
  }

  cout << "Test6 complete\n";
}

void test7()
{
  cout << "Test 7: Tests Sound::detach()\n";

  cout << "Play same spell3 3 times 500ms apart without detach()\n";
  spell3.play();
  update_ms(500);
  spell3.play();
  update_ms(500);
  spell3.play();
  update_ms(2000);

  cout << "Play same spell3 3 times 500ms apart with detach()\n";
  spell3.play();
  spell3.detach();
  update_ms(500);
  spell3.play();
  spell3.detach();
  update_ms(500);
  spell3.play();
  spell3.detach();
  update_ms(500);
  update_ms(2000);

  cout << "Test7 complete\n";
}

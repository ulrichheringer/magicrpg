#include <allegro5/allegro5.h>
#include <allegro5/allegro_font.h>
#include <allegro5/allegro_primitives.h>
#include <allegro5/allegro_ttf.h>
#include <math.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// --- Game Constants ---
const int SCREEN_W = 1280;
const int SCREEN_H = 720;
const float FPS = 120.0;
const int STAGES_TO_WIN = 3;

// --- Entity Constants ---
const int MAX_PLATFORMS = 50;
const int MAX_PROJECTILES = 30;
const int MAX_MONSTERS = 10;
const float PLAYER_SIZE = 30.0;
const float PLAYER_SPEED = 8.0;
const float GRAVITY = 0.3;
const float JUMP_STRENGTH = -13.0;
const float PROJECTILE_SIZE = 4.0;
const float PROJECTILE_SPEED = 20.0;
const float MONSTER_SIZE = 32.0;
const int MONSTER_HEALTH = 3;
const float KEY_SIZE = 16.0;
const float DOOR_WIDTH = 40.0;
const float DOOR_HEIGHT = 80.0;
const int PLAYER_STARTING_LIVES = 3;
const float PLAYER_INVINCIBILITY_DURATION = 1.5;

// --- AI Constants ---
const int MAX_MONSTER_PROJECTILES = 20;
const float MONSTER_PROJECTILE_SPEED = 10.0;
const float MONSTER_AGGRO_RANGE = 650.0;
const float MONSTER_SHOOT_COOLDOWN = 1.0;
const float MONSTER_CHECK_RADIUS = 300.0; // NEW: Radius for proximity check
const int MAX_MONSTERS_IN_RADIUS =
    1; // NEW: Max monsters allowed in that radius

// --- World Generation Constants ---
const float GROUND_Y = 630.0;
const float PLATFORM_HEIGHT = 50.0;
const int CHUNK_WIDTH = 190;
const int MIN_SEGMENT_CHUNKS = 3;
const int MAX_SEGMENT_CHUNKS = 8;
const int MIN_GAP_CHUNKS = 2;
const int MAX_GAP_CHUNKS = 4;
const int LANE_CONTINUITY_CHANCE = 60;
const int MONSTER_SPAWN_CHANCE = 50;
const float platform_lanes[] = {500.0, 360.0, 240.0}; // FIX: Increased spacing
const int CULLING_BUFFER = 3000;

// --- Game State Management & Structures ---
typedef enum { PLAYING, QUESTION, WON, GAME_OVER } GameState;
typedef struct {
  float x, y;
  float vx, vy;
  bool on_ground;
} Player;
typedef struct {
  float x, y;
  float width;
  float height;
  bool active;
} Barrier;
typedef struct {
  float x, y;
  float width;
  float height;
} Platform;
typedef struct {
  float x, y;
  float vx, vy;
  bool active;
} Projectile;
typedef struct {
  float x, y;
  int health;
  bool active;
  float shoot_cooldown;
} Monster;
typedef struct {
  float x, y;
  bool active;
  bool collected;
} Key;
typedef struct {
  float x, y;
  bool active;
  bool opened;
} Door;
typedef struct {
  char question[256];
  char answers[4][128];
  int correct_answer_idx;
} MathQuestion;
typedef struct {
  float last_x;
  int chunks_left;
  bool is_gap;
} LaneState;

// --- Global state ---
GameState game_state = PLAYING;
int player_lives = PLAYER_STARTING_LIVES;
float player_invincibility_timer = 0;
float screen_flash_alpha = 0;
MathQuestion current_question;
int selected_answer = 0;
int stage_count = 1;
bool can_spawn_new_wave = true;

// Helper functions
int random_int(int min, int max) { return min + rand() % (max - min + 1); }
void take_damage(Player *player) {
  if (player_invincibility_timer > 0)
    return;
  player_lives--;
  player_invincibility_timer = PLAYER_INVINCIBILITY_DURATION;
  screen_flash_alpha = 150;
  if (player_lives <= 0) {
    game_state = GAME_OVER;
  }
}
void load_random_question(sqlite3 *db, MathQuestion *q) {
  sqlite3_stmt *res;
  const char *sql = "SELECT question, answer1, answer2, answer3, answer4, "
                    "correctAnswer FROM maths ORDER BY RANDOM() LIMIT 1;";
  if (sqlite3_prepare_v2(db, sql, -1, &res, 0) != SQLITE_OK) {
    fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
    return;
  }
  if (sqlite3_step(res) == SQLITE_ROW) {
    snprintf(q->question, sizeof(q->question), "%s",
             sqlite3_column_text(res, 0));
    snprintf(q->answers[0], sizeof(q->answers[0]), "1. %s",
             sqlite3_column_text(res, 1));
    snprintf(q->answers[1], sizeof(q->answers[1]), "2. %s",
             sqlite3_column_text(res, 2));
    snprintf(q->answers[2], sizeof(q->answers[2]), "3. %s",
             sqlite3_column_text(res, 3));
    snprintf(q->answers[3], sizeof(q->answers[3]), "4. %s",
             sqlite3_column_text(res, 4));
    q->correct_answer_idx = sqlite3_column_int(res, 5) - 1;
  }
  sqlite3_finalize(res);
}

// NEW: Helper function to check monster density
bool is_spawn_location_valid(float cx, float cy,
                             Monster monsters[MAX_MONSTERS]) {
  int nearby_count = 0;
  for (int i = 0; i < MAX_MONSTERS; i++) {
    if (monsters[i].active) {
      float dx = monsters[i].x - cx;
      float dy = monsters[i].y - cy;
      if (sqrt(dx * dx + dy * dy) < MONSTER_CHECK_RADIUS) {
        nearby_count++;
      }
    }
  }
  return nearby_count < MAX_MONSTERS_IN_RADIUS;
}

int main(void) {
  al_init();
  al_install_keyboard();
  al_install_mouse();
  al_init_primitives_addon();
  al_init_font_addon();
  al_init_ttf_addon();
  ALLEGRO_TIMER *timer = al_create_timer(1.0 / FPS);
  ALLEGRO_DISPLAY *display = al_create_display(SCREEN_W, SCREEN_H);
  ALLEGRO_EVENT_QUEUE *event_queue = al_create_event_queue();
  ALLEGRO_FONT *font = al_load_ttf_font("pirulen.ttf", 32, 0);
  ALLEGRO_FONT *ui_font = al_load_ttf_font("pirulen.ttf", 24, 0);
  if (!font || !ui_font) {
    fprintf(stderr, "Could not load 'pirulen.ttf'.\n");
    return -1;
  }
  sqlite3 *db;
  if (sqlite3_open("questions.db", &db)) {
    fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
    return -1;
  }
  al_register_event_source(event_queue, al_get_display_event_source(display));
  al_register_event_source(event_queue, al_get_timer_event_source(timer));
  al_register_event_source(event_queue, al_get_keyboard_event_source());
  al_register_event_source(event_queue, al_get_mouse_event_source());
  srand(time(NULL));

  Player player = {100, 100, 0, 0, false};
  Platform ground_segments[3];
  Platform platforms[MAX_PLATFORMS];
  Projectile projectiles[MAX_PROJECTILES];
  Projectile monster_projectiles[MAX_MONSTER_PROJECTILES];
  Monster monsters[MAX_MONSTERS];
  Key key = {0, 0, false, false};
  Door door = {0, 0, false, false};
  Barrier barrier = {0, 0, 10, SCREEN_H, false};

  int num_platforms = 0;
  float camera_x = 0;
  LaneState lane_states[3] = {
      {400, 5, false}, {500, 6, false}, {600, 4, false}};
  bool wave_in_progress = false;
  int monsters_to_spawn = 0;
  int active_monster_count = 0;
  float last_monster_x = 0;

  ground_segments[0] =
      (Platform){-SCREEN_W, GROUND_Y, SCREEN_W, SCREEN_H - GROUND_Y};
  ground_segments[1] = (Platform){0, GROUND_Y, SCREEN_W, SCREEN_H - GROUND_Y};
  ground_segments[2] =
      (Platform){SCREEN_W, GROUND_Y, SCREEN_W, SCREEN_H - GROUND_Y};
  for (int i = 0; i < MAX_PROJECTILES; i++) {
    projectiles[i].active = false;
  }
  for (int i = 0; i < MAX_MONSTER_PROJECTILES; i++) {
    monster_projectiles[i].active = false;
  }
  for (int i = 0; i < MAX_MONSTERS; i++) {
    monsters[i].active = false;
  }

  bool keys[ALLEGRO_KEY_MAX] = {false};
  bool running = true;
  bool redraw = true;

  al_start_timer(timer);
  while (running) {
    ALLEGRO_EVENT event;
    al_wait_for_event(event_queue, &event);

    if (event.type == ALLEGRO_EVENT_TIMER) {
      if (game_state != PLAYING) {
        redraw = true;
        continue;
      }
      if (player_invincibility_timer > 0)
        player_invincibility_timer -= 1.0 / FPS;
      if (screen_flash_alpha > 0)
        screen_flash_alpha -= 5;
      player.vx = 0;
      if (keys[ALLEGRO_KEY_A])
        player.vx = -PLAYER_SPEED;
      if (keys[ALLEGRO_KEY_D])
        player.vx = PLAYER_SPEED;
      if (keys[ALLEGRO_KEY_W] && player.on_ground) {
        player.vy = JUMP_STRENGTH;
        player.on_ground = false;
      }
      player.vy += GRAVITY;
      float next_x = player.x + player.vx;
      float next_y = player.y + player.vy;
      player.on_ground = false;
      for (int i = 0; i < 3; i++) {
        if (next_x + PLAYER_SIZE > ground_segments[i].x &&
            next_x < ground_segments[i].x + ground_segments[i].width &&
            next_y + PLAYER_SIZE > ground_segments[i].y) {
          next_y = ground_segments[i].y - PLAYER_SIZE;
          player.vy = 0;
          player.on_ground = true;
        }
      }
      for (int i = 0; i < num_platforms; i++) {
        if (next_x + PLAYER_SIZE > platforms[i].x &&
            next_x < platforms[i].x + platforms[i].width &&
            next_y + PLAYER_SIZE > platforms[i].y &&
            next_y < platforms[i].y + platforms[i].height) {
          if (player.vy >= 0 &&
              player.y + PLAYER_SIZE <= platforms[i].y + (GRAVITY + 1)) {
            if (!keys[ALLEGRO_KEY_S] && !keys[ALLEGRO_KEY_DOWN]) {
              next_y = platforms[i].y - PLAYER_SIZE;
              player.vy = 0;
              player.on_ground = true;
            }
          }
        }
      }
      if (barrier.active && next_x + PLAYER_SIZE > barrier.x &&
          next_x < barrier.x + barrier.width) {
        next_x = barrier.x - PLAYER_SIZE;
      }
      player.x = next_x;
      player.y = next_y;
      camera_x = player.x - (SCREEN_W / 3.0);
      for (int i = 0; i < 3; i++) {
        if (ground_segments[i].x + SCREEN_W < camera_x) {
          ground_segments[i].x += 3 * SCREEN_W;
        }
        if (ground_segments[i].x > camera_x + SCREEN_W) {
          ground_segments[i].x -= 3 * SCREEN_W;
        }
      }

      if (can_spawn_new_wave && random_int(1, 100) <= MONSTER_SPAWN_CHANCE) {
        wave_in_progress = true;
        can_spawn_new_wave = false;
        monsters_to_spawn = random_int(6, 10);
      }

      // REFACTORED: Platform & Monster Generation
      for (int i = 0; i < 3; i++) {
        while (lane_states[i].last_x < camera_x + SCREEN_W + CHUNK_WIDTH) {
          if (lane_states[i].chunks_left <= 0) {
            if (random_int(1, 100) > LANE_CONTINUITY_CHANCE) {
              lane_states[i].is_gap = true;
              lane_states[i].chunks_left =
                  random_int(MIN_GAP_CHUNKS, MAX_GAP_CHUNKS);
            } else {
              lane_states[i].is_gap = false;
              lane_states[i].chunks_left =
                  random_int(MIN_SEGMENT_CHUNKS, MAX_SEGMENT_CHUNKS);
            }
          }
          if (!lane_states[i].is_gap && num_platforms < MAX_PLATFORMS) {
            platforms[num_platforms] =
                (Platform){lane_states[i].last_x, platform_lanes[i],
                           CHUNK_WIDTH, PLATFORM_HEIGHT};
            if (monsters_to_spawn > 0) {
              float candidate_x = platforms[num_platforms].x +
                                  platforms[num_platforms].width / 2;
              float candidate_y = platforms[num_platforms].y - MONSTER_SIZE;
              if (is_spawn_location_valid(candidate_x, candidate_y, monsters)) {
                for (int m = 0; m < MAX_MONSTERS; m++) {
                  if (!monsters[m].active) {
                    monsters[m].active = true;
                    monsters[m].health = MONSTER_HEALTH;
                    monsters[m].shoot_cooldown =
                        MONSTER_SHOOT_COOLDOWN + (random_int(0, 10) / 10.0);
                    monsters[m].x = candidate_x - MONSTER_SIZE / 2;
                    monsters[m].y = candidate_y;
                    active_monster_count++;
                    last_monster_x = monsters[m].x;
                    monsters_to_spawn--;
                    break;
                  }
                }
              }
            }
            num_platforms++;
          }
          lane_states[i].last_x += CHUNK_WIDTH;
          lane_states[i].chunks_left--;
        }
      }
      if (wave_in_progress && monsters_to_spawn == 0 && !door.active) {
        door.active = true;
        door.opened = false;
        door.x = last_monster_x + random_int(1200, 1800);
        door.y = GROUND_Y - DOOR_HEIGHT;
        barrier.active = true;
        barrier.x = DOOR_WIDTH + door.x;
      }
      for (int i = 0; i < num_platforms; i++) {
        if (platforms[i].x + platforms[i].width < camera_x - CULLING_BUFFER) {
          for (int j = i; j < num_platforms - 1; j++) {
            platforms[j] = platforms[j + 1];
          }
          num_platforms--;
          i--;
        }
      }

      for (int i = 0; i < MAX_MONSTERS; i++) {
        if (monsters[i].active) {
          if (player.x + PLAYER_SIZE > monsters[i].x &&
              player.x < monsters[i].x + MONSTER_SIZE &&
              player.y + PLAYER_SIZE > monsters[i].y &&
              player.y < monsters[i].y + MONSTER_SIZE) {
            take_damage(&player);
          }
          monsters[i].shoot_cooldown -= 1.0 / FPS;
          float dx = player.x - monsters[i].x;
          float dy = player.y - monsters[i].y;
          float distance = sqrt(dx * dx + dy * dy);
          if (distance < MONSTER_AGGRO_RANGE &&
              monsters[i].shoot_cooldown <= 0) {
            monsters[i].shoot_cooldown = MONSTER_SHOOT_COOLDOWN;
            for (int j = 0; j < MAX_MONSTER_PROJECTILES; j++) {
              if (!monster_projectiles[j].active) {
                monster_projectiles[j].active = true;
                monster_projectiles[j].x = monsters[i].x + MONSTER_SIZE / 2;
                monster_projectiles[j].y = monsters[i].y + MONSTER_SIZE / 2;
                monster_projectiles[j].vx =
                    (dx / distance) * MONSTER_PROJECTILE_SPEED;
                monster_projectiles[j].vy =
                    (dy / distance) * MONSTER_PROJECTILE_SPEED;
                break;
              }
            }
          }
        }
      }
      for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (projectiles[i].active) {
          projectiles[i].x += projectiles[i].vx;
          projectiles[i].y += projectiles[i].vy;
          for (int j = 0; j < MAX_MONSTERS; j++) {
            if (monsters[j].active) {
              if (projectiles[i].x > monsters[j].x &&
                  projectiles[i].x < monsters[j].x + MONSTER_SIZE &&
                  projectiles[i].y > monsters[j].y &&
                  projectiles[i].y < monsters[j].y + MONSTER_SIZE) {
                projectiles[i].active = false;
                monsters[j].health--;
                if (monsters[j].health <= 0) {
                  monsters[j].active = false;
                  active_monster_count--;
                  if (wave_in_progress && active_monster_count <= 0) {
                    key.active = true;
                    key.collected = false;
                    key.x = monsters[j].x + MONSTER_SIZE / 2;
                    key.y = monsters[j].y + MONSTER_SIZE / 2;
                    wave_in_progress = false;
                  }
                }
                break;
              }
            }
          }
          if (projectiles[i].y < 0 || projectiles[i].y > SCREEN_H ||
              projectiles[i].x < camera_x - CULLING_BUFFER ||
              projectiles[i].x > camera_x + SCREEN_W + CULLING_BUFFER) {
            projectiles[i].active = false;
          }
        }
      }
      for (int i = 0; i < MAX_MONSTER_PROJECTILES; i++) {
        if (monster_projectiles[i].active) {
          monster_projectiles[i].x += monster_projectiles[i].vx;
          monster_projectiles[i].y += monster_projectiles[i].vy;
          if (player.x + PLAYER_SIZE > monster_projectiles[i].x &&
              player.x < monster_projectiles[i].x + PROJECTILE_SIZE &&
              player.y + PLAYER_SIZE > monster_projectiles[i].y &&
              player.y < monster_projectiles[i].y + PROJECTILE_SIZE) {
            monster_projectiles[i].active = false;
            take_damage(&player);
          }
          if (monster_projectiles[i].y < 0 ||
              monster_projectiles[i].y > SCREEN_H ||
              monster_projectiles[i].x < camera_x - CULLING_BUFFER ||
              monster_projectiles[i].x > camera_x + SCREEN_W + CULLING_BUFFER) {
            monster_projectiles[i].active = false;
          }
        }
      }
      if (key.active) {
        if (player.x + PLAYER_SIZE > key.x && player.x < key.x + KEY_SIZE &&
            player.y + PLAYER_SIZE > key.y && player.y < key.y + KEY_SIZE) {
          key.active = false;
          key.collected = true;
        }
      }
      if (door.active && !door.opened) {
        if (player.x + PLAYER_SIZE > door.x && player.x < door.x + DOOR_WIDTH &&
            player.y + PLAYER_SIZE > door.y &&
            player.y < door.y + DOOR_HEIGHT) {
          if (key.collected) {
            game_state = QUESTION;
            load_random_question(db, &current_question);
            selected_answer = 0;
          }
        }
      }
      redraw = true;

    } else if (event.type == ALLEGRO_EVENT_DISPLAY_CLOSE) {
      running = false;
    } else if (event.type == ALLEGRO_EVENT_KEY_DOWN) {
      if (game_state == PLAYING) {
        keys[event.keyboard.keycode] = true;
      } else if (game_state == QUESTION) {
        switch (event.keyboard.keycode) {
        case ALLEGRO_KEY_W:
        case ALLEGRO_KEY_UP:
          selected_answer--;
          if (selected_answer < 0)
            selected_answer = 3;
          break;
        case ALLEGRO_KEY_S:
        case ALLEGRO_KEY_DOWN:
          selected_answer++;
          if (selected_answer > 3)
            selected_answer = 0;
          break;
        case ALLEGRO_KEY_ENTER:
        case ALLEGRO_KEY_SPACE:
          if (selected_answer == current_question.correct_answer_idx) {
            barrier.active = false;
            if (stage_count >= STAGES_TO_WIN) {
              game_state = WON;
            } else {
              stage_count++;
              door.active = false;
              key.collected = false;
              can_spawn_new_wave = true;
              game_state = PLAYING;
            }
          } else {
            take_damage(&player);
            game_state = PLAYING;
          }
          break;
        }
      }
    } else if (event.type == ALLEGRO_EVENT_KEY_UP) {
      keys[event.keyboard.keycode] = false;
    } else if (event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN) {
      if (game_state == PLAYING && event.mouse.button == 1) {
        for (int i = 0; i < MAX_PROJECTILES; i++) {
          if (!projectiles[i].active) {
            projectiles[i].active = true;
            float start_x = player.x + PLAYER_SIZE / 2;
            float start_y = player.y + PLAYER_SIZE / 2;
            projectiles[i].x = start_x;
            projectiles[i].y = start_y;
            float target_world_x = event.mouse.x + camera_x;
            float target_world_y = event.mouse.y;
            float dx = target_world_x - start_x;
            float dy = target_world_y - start_y;
            float distance = sqrt(dx * dx + dy * dy);
            if (distance > 0) {
              projectiles[i].vx = (dx / distance) * PROJECTILE_SPEED;
              projectiles[i].vy = (dy / distance) * PROJECTILE_SPEED;
            }
            break;
          }
        }
      }
    }

    // --- Rendering ---
    if (redraw && al_is_event_queue_empty(event_queue)) {
      redraw = false;
      al_clear_to_color(al_map_rgb(20, 20, 40));
      for (int i = 0; i < 3; i++) {
        al_draw_filled_rectangle(
            ground_segments[i].x - camera_x, ground_segments[i].y,
            ground_segments[i].x + ground_segments[i].width - camera_x,
            ground_segments[i].y + ground_segments[i].height,
            al_map_rgb(80, 180, 80));
      }
      for (int i = 0; i < num_platforms; i++) {
        al_draw_filled_rectangle(platforms[i].x - camera_x, platforms[i].y,
                                 platforms[i].x + platforms[i].width - camera_x,
                                 platforms[i].y + platforms[i].height,
                                 al_map_rgb(100, 100, 120));
      }
      if (barrier.active) {
        al_draw_filled_rectangle(barrier.x - camera_x, barrier.y,
                                 barrier.x + barrier.width - camera_x,
                                 barrier.y + barrier.height,
                                 al_map_rgba(255, 0, 0, 100));
      }
      for (int i = 0; i < MAX_MONSTERS; i++) {
        if (monsters[i].active) {
          al_draw_filled_rectangle(monsters[i].x - camera_x, monsters[i].y,
                                   monsters[i].x + MONSTER_SIZE - camera_x,
                                   monsters[i].y + MONSTER_SIZE,
                                   al_map_rgb(200, 50, 50));
        }
      }
      if (key.active) {
        al_draw_filled_rectangle(key.x - camera_x, key.y,
                                 key.x + KEY_SIZE - camera_x, key.y + KEY_SIZE,
                                 al_map_rgb(255, 223, 0));
      }
      if (door.active) {
        ALLEGRO_COLOR door_color =
            door.opened ? al_map_rgb(100, 255, 100) : al_map_rgb(139, 69, 19);
        al_draw_filled_rectangle(door.x - camera_x, door.y,
                                 door.x + DOOR_WIDTH - camera_x,
                                 door.y + DOOR_HEIGHT, door_color);
      }
      for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (projectiles[i].active) {
          al_draw_filled_circle(projectiles[i].x - camera_x, projectiles[i].y,
                                PROJECTILE_SIZE, al_map_rgb(255, 255, 0));
        }
      }
      for (int i = 0; i < MAX_MONSTER_PROJECTILES; i++) {
        if (monster_projectiles[i].active) {
          al_draw_filled_circle(monster_projectiles[i].x - camera_x,
                                monster_projectiles[i].y, PROJECTILE_SIZE,
                                al_map_rgb(255, 100, 0));
        }
      }
      if (player_invincibility_timer <= 0 ||
          (int)(player_invincibility_timer * 10) % 2 == 0) {
        al_draw_filled_rectangle(
            player.x - camera_x, player.y, player.x + PLAYER_SIZE - camera_x,
            player.y + PLAYER_SIZE, al_map_rgb(255, 100, 100));
      }
      if (screen_flash_alpha > 0) {
        al_draw_filled_rectangle(
            0, 0, SCREEN_W, SCREEN_H,
            al_map_rgba(255, 0, 0, (int)screen_flash_alpha));
      }
      if (game_state == QUESTION) {
        al_draw_filled_rectangle(100, 100, SCREEN_W - 100, SCREEN_H - 100,
                                 al_map_rgba(0, 0, 0, 200));
        al_draw_multiline_text(font, al_map_rgb(255, 255, 255), SCREEN_W / 2,
                               150, SCREEN_W - 240, 40, ALLEGRO_ALIGN_CENTER,
                               current_question.question);
        for (int i = 0; i < 4; i++) {
          ALLEGRO_COLOR color = (i == selected_answer)
                                    ? al_map_rgb(255, 255, 0)
                                    : al_map_rgb(255, 255, 255);
          al_draw_text(ui_font, color, SCREEN_W / 2, 300 + i * 60,
                       ALLEGRO_ALIGN_CENTER, current_question.answers[i]);
        }
      }
      al_draw_textf(ui_font, al_map_rgb(255, 255, 255), 20, 20, 0, "Lives: %d",
                    player_lives);
      al_draw_textf(ui_font, al_map_rgb(255, 255, 255), 20, 50, 0,
                    "Stage: %d / %d", stage_count, STAGES_TO_WIN);
      if (key.collected) {
        al_draw_text(ui_font, al_map_rgb(255, 223, 0), 20, 80, 0,
                     "Key Obtained!");
      }
      if (game_state == WON) {
        al_draw_text(font, al_map_rgb(255, 255, 255), SCREEN_W / 2,
                     SCREEN_H / 2 - 20, ALLEGRO_ALIGN_CENTER, "YOU WIN!");
      }
      if (game_state == GAME_OVER) {
        al_draw_text(font, al_map_rgb(255, 50, 50), SCREEN_W / 2,
                     SCREEN_H / 2 - 20, ALLEGRO_ALIGN_CENTER, "GAME OVER");
      }
      al_flip_display();
    }
  }
  sqlite3_close(db);
  al_destroy_font(font);
  al_destroy_font(ui_font);
  al_destroy_timer(timer);
  al_destroy_display(display);
  al_destroy_event_queue(event_queue);
  al_shutdown_primitives_addon();
  al_uninstall_mouse();
  al_uninstall_keyboard();
  return 0;
}

#include <SFML/Graphics.hpp>
#include "imgui.h"
#include "imgui-SFML.h"
#include <cmath>
#include <memory>
#include <random>
#include <type_traits>
#include <vector>
#include <array>
#include <iostream>

/*
* Known "issues":
* // NOTE: Unfortunately because of the height of the pipes asset, the Top obstacle lowest point is -140, so in gameplay it doesnt look so random and gives the illusion that the obstacles are usually in the top zone of the screen.
*/

/*
* Flappy bird:
* Uses a maximum of 3 pairs of obstacles at the same time.
* When a pair is exiting the left border of the screen, a new pair on the right border appears.
* 
* Player scores 1 point every time he exceeds the x center of a pair of obstacles.
*/

/*
* OK so I think I should do this for the game loop:
* world updates entities.
* Entities update position.
* World checks for bird collision.
* bird applies impulse and gravity.
*/


using f32 = float;
using i32 = int;
using u32 = unsigned int;

// @return A random value in the range [min, max].
template <typename T>
T Random(T min, T max)
{
	assert(min < max && "Random: min must be less than max.");

	static std::random_device rd;
	static std::mt19937_64 gen(rd());

	static_assert(
		std::is_integral_v<T> || std::is_floating_point_v<T>,
		"T must be an integer or floating-point type"
		);

	if constexpr (std::is_integral_v<T>) {
		std::uniform_int_distribution<T> dist(min, max);
		return dist(gen);
	}
	else {
		// uniform_real_distribution es [min, max),
		// así que movemos max al siguiente valor representable.
		T inclusiveMax = std::nextafter(
			max,
			std::numeric_limits<T>::infinity()
		);

		std::uniform_real_distribution<T> dist(min, inclusiveMax);
		return dist(gen);
	}
}

namespace RenderOrder
{
	inline constexpr u32 TOP_UI = 1U;
	inline constexpr u32 BOTTTOM = 4294967295U; // Last possible element.
	inline constexpr u32 GAMEPLAY = BOTTTOM / 2U; // Halfway to both limits to give room for other possibilities.
	inline constexpr u32 BACKGROUND = BOTTTOM * 3U / 4U; // Between GAMEPLAY AND BOTTOM. NOTE: this value was decided quickly, if you find a more suitable value don't hesitate to change this in the final "engine".
}

class Entity
{
public:
	Entity(sf::Sprite sprite, u32 renderOrder = RenderOrder::GAMEPLAY)
		: sprite_(std::move(sprite)), renderOrder_(renderOrder) {}
	virtual ~Entity() = default;

	virtual void Update(const f32 deltaTime) = 0;


	void CenterOrigin()
	{
		const auto bounds = sprite_.getLocalBounds().size;
		sprite_.setOrigin({ bounds.x / 2 , bounds.y / 2 });
	}
	
	inline sf::Sprite& GetSprite() { return sprite_; }
	inline u32 GetRenderOrder() const { return renderOrder_; }
	inline auto GetHalfWidth() const { return sprite_.getLocalBounds().size.x / 2; }
	inline auto GetHalfHeight() const { return sprite_.getLocalBounds().size.y / 2; }
protected:
	sf::Sprite sprite_;
private:
	u32 renderOrder_;
};

class World
{
public:
	explicit World(sf::RenderWindow& window) : window_(window) {}
	virtual ~World() = default;

	template<typename T, typename... Args>
	T* Add(Args&&... args)
	{
		static_assert(std::is_base_of_v<Entity, T>, "T must derive from Entity.");
		auto ent = std::make_unique<T>(std::forward<Args>(args)...);
		T* ptr = ent.get();
		ent->CenterOrigin();
		entities_.push_back(std::move(ent));
		return ptr;
	}

	// Updates all entities.
	virtual void Update(const f32 deltaTime)
	{
		for (auto& ent : entities_)
		{
			ent->Update(deltaTime);
		}
	}

	virtual void HandleInput(const std::optional<sf::Event>& event)
	{
	}

	// Helper to modify entities values. Override this.
	virtual void OnImGuiUpdateValues()
	{
		//
	}

	// Personally I don't like the idea of the world
	// being responsible for rendering, but I want to keep this game as
	// small and simple as possible.
#pragma region RENDERING

	// Render everything.
	void RenderAll(sf::RenderWindow& window) const
	{
		struct RenderItem
		{
			const sf::Sprite* sprite;
			u32 depth;
		};

		// This sorting-every-frame design is not optimal, but will work for the
		// current project.
		// TODO: implement optimization like dirty flag.
		std::vector<RenderItem> items;
		items.reserve(entities_.size() + additionalSprites_.size());

		for (const auto& ent : entities_)
			items.push_back({ &ent->GetSprite(), ent->GetRenderOrder() });
		for (const auto& [sprite, depth] : additionalSprites_)
			items.push_back({ sprite, depth });

		std::stable_sort(items.begin(), items.end(), [](const RenderItem& a, const RenderItem& b)
			{
				return a.depth > b.depth;
			});

		for (const auto& item : items)
		{
			window.draw(*item.sprite);
			// std::cout << item.depth << std::endl;
		}
		
	}

	// Registra un sprite extra (no perteneciente al sprite_ base de una Entity)
	// para que se dibuje junto a todo lo demás, respetando su propia profundidad.
	void AddAdditionalSprite(const sf::Sprite* sprite, u32 renderDepth)
	{
		additionalSprites_.push_back({ sprite, renderDepth });
	}

#pragma endregion

	inline auto* GetFirst() const { return entities_.empty() ? nullptr : entities_[0].get(); }
protected:
	sf::RenderWindow& window_; // Easy access to window to call getSize(), etc.
	std::vector<std::unique_ptr<Entity>> entities_;
	std::vector<std::pair<const sf::Sprite*, u32>> additionalSprites_; // Default render loop renders all entities' sprites, but in some cases an entity has several sprites to render.
};

template<typename TWorld>
class App
{
public:
	template<typename... Args>
	explicit App(sf::RenderWindow& window, Args&&... worldArgs) : window_(window)
	{
		static_assert(std::is_base_of_v<World, TWorld>, "TWorld must derive from World");
		world_ = CreateWorld(std::forward<Args>(worldArgs)...);

		if (!ImGui::SFML::Init(window))
			throw std::runtime_error("ImGui-SFML FAILED TO INITIALIZE.");
	}

	~App()
	{
		ImGui::SFML::Shutdown();
	}

	template<typename... Args>
	std::unique_ptr<TWorld> CreateWorld(Args&&... worldArgs)
	{
		static_assert(std::is_base_of_v<World, TWorld>, "TWorld must derive from World");
		return std::make_unique<TWorld>(window_, std::forward<Args>(worldArgs)...);
	}

	// Maybe I won't use this in this project, but maybe needed in future projects.
	template<typename... Args>
	void ChangeLevel(Args&&... args)
	{
		world_.reset();
		world_ = CreateWorld(std::forward<Args>(args)...);
	}

	// Loop.
	void Run()
	{
		assert(world_);

		while (window_.isOpen())
		{
			// Handle events
			while (const std::optional event = window_.pollEvent())
			{
				ImGui::SFML::ProcessEvent(window_, *event);
				// Close window event.
				if (event->is<sf::Event::Closed>())
					window_.close();

				world_->HandleInput(event);
			}
			const auto time = deltaTimeClock.restart();
			const f32 deltaTime = time.asSeconds();
			
			// IMGUI drawing.
			ImGui::SFML::Update(window_, time);
			ImGui::Begin("BEGIN IMGUI");
#ifdef _DEBUG
			DrawImGuiStuff();
			world_->OnImGuiUpdateValues();
#endif

			ImGui::End();


			world_->Update(deltaTime);

			window_.clear();
			// Draw.
			
			world_->RenderAll(window_);
			ImGui::SFML::Render(window_);

			window_.display();
		}
	}
	
	World* GetWorld() { return world_.get(); }

protected:
	// Override this to draw imgui elements. Begin is called before and IMGUI::END after, so it's safe.
	virtual void DrawImGuiStuff() const
	{
		//
	}

private:
	sf::RenderWindow& window_;
	std::unique_ptr<World> world_;
	sf::Clock deltaTimeClock;
};

namespace FlappyBirdGame
{
	namespace RenderOrder
	{
		inline constexpr u32 FLOOR = ::RenderOrder::GAMEPLAY + 1; // A bit deeper than normal gameplay elements.
		inline constexpr u32 OBSTACLES = FLOOR + 1; // Obstacles are behind the floor.
	}

	class Bird : public Entity
	{
	public:
		
		
		Bird() : Entity(MakeSprite())
		{
			sprite_.setTextureRect(sf::IntRect(
				{ 0,0 },
				{ SPRITE_SIZE.x, SPRITE_SIZE.y}
			));
		}


		void Update(const f32 deltaTime) override
		{
			Animate(deltaTime);
			
			// New velocity.
			velY_ = velY_ + GRAVITY * deltaTime;
			velY_ = std::clamp(velY_, FLAP_IMPULSE, -FLAP_IMPULSE * 2);
			// Apply rotation.

			// LEARNING NOTE:
			// Tried to apply rotation using the rotation included in the Sprite,
			// but when calling getRotation, that rotation is normalized to 
			// [0,360], breaking my logic.
			// Solution: using a separate rotation variable.
			constexpr f32 ROTATION_LIMIT = 35.f;
			const auto GOING_UP = velY_ < 0; // NOTE TO MYSELF: SFML has inverted Y values.
			const f32 targetRotation = GOING_UP ? -ROTATION_LIMIT : ROTATION_LIMIT;
			rotation_ = std::lerp(rotation_, targetRotation,
				GOING_UP ? rotationSpeed_ * 0.2f : // When going upwards rotation is fast but still perceptible.
				deltaTime * rotationSpeed_); // When falling, bit by bit.

			rotation_ = std::clamp(rotation_, -ROTATION_LIMIT, ROTATION_LIMIT);
			sprite_.setRotation(sf::degrees(rotation_));
			
			
			// Update position.

			const auto oldY = sprite_.getPosition().y;
			const auto newY = oldY + velY_ * deltaTime;
			sprite_.setPosition({
					sprite_.getPosition().x,
					newY
			});
		}

		void Flap()
		{
			velY_ = FLAP_IMPULSE;
		}

		f32 GetVelY() const noexcept { return velY_; }

	private:
		// Bird uses spritesheet;
		static sf::Sprite MakeSprite()
		{
			static sf::Texture texture = []
				{
					sf::Texture tex;
					if (!tex.loadFromFile(SPRITE_SHEET_PATH))
						throw std::runtime_error("Could not load bird texture.");
					return tex;
				}();
			return sf::Sprite(texture);
		}

		void Animate(const f32 deltaTime)
		{
			static f32 elapsedTimeSinceLastAnimUpdate = 0.f;
			elapsedTimeSinceLastAnimUpdate += deltaTime;


			// I hate the need of using an "Update" approach every frame,
			// specially because of the need for an "if", but I could not find a better
			// implementation. I also thought of using a while loop, but I read
			// it evaluates to something similar to an if, therefore being prone to
			// branch misprediction as well.
			if (elapsedTimeSinceLastAnimUpdate < UPDATE_ANIM_SPAN) return;


			// Clean elapsed time.
			elapsedTimeSinceLastAnimUpdate -= UPDATE_ANIM_SPAN;

			// Up->middle->down->middle->up. Loop this.
			static i32 frame = 0;
			static i32 direction = 1; // 1 means go up, -1 means go down.
			static sf::IntRect section;
			section.size = { SPRITE_SIZE.x, SPRITE_SIZE.y};

			switch (frame)
			{
			case 0:
				section.position = { 0,0 };
				break;
			case 1:
				section.position = { SPRITE_SIZE.x, 0 };
				break;
			case 2:
				section.position = { SPRITE_SIZE.x * 2, 0 };
				break;
			}
			sprite_.setTextureRect(section);
			
			frame += direction;
			// Exceeds limits? reverse state direction.
			if (frame >= 2 || frame <= 0)
			{
				direction *= -1;
				// LOG std::cout << "Reached the border of an animation... inversed direction." << std::endl;
			}
			// LOG std::cout << "Updated BIRD animation" << std::endl;
		}

		f32 velY_ = 0.f;
		f32 rotation_ = 0.f;
		f32 rotationSpeed_ = 2.f;

		// CONSTANTS

		// Gameplay.
		static constexpr f32 GRAVITY = 900.f;
		static constexpr f32 FLAP_IMPULSE = -350.f;

		// Sprites.
	public:
		static constexpr sf::Vector2u SPRITE_SIZE = { 34u,24u }; // I'm not sure why but the sprite size is 34 px width (in the spritesheet), but original sprites are 32 px.
	private:
		static constexpr std::string_view SPRITE_SHEET_PATH = "Assets/spritesheetYellow.png";

		// Animation.
		static constexpr f32 UPDATE_ANIM_SPAN = 0.1f;
	};

	
	class Floor : public Entity
	{
	public:
		explicit Floor() : Entity(MakeSprite(), RenderOrder::FLOOR)
		{
			// Define the final size of the sprite (495 x 112).
			// World will use this to center the origin.
			UpdateTextureRect();

			// On X we center the sprite on its own width so it starts in x = 0;
			sprite_.setPosition({ VISIBLE_TEXTURE_WIDTH / 2.f, FLOOR_Y });
		}

		void Update(const f32 deltaTime) override
		{
			// LEARNING NOTE:
			// In order for the content to go left, the "window" of the texture
			// must go to the right, thats why we subtract.
			uvOffset_ -= SPEED * deltaTime;

			// Texture repeats, restart accumulator.
			if (uvOffset_ >= TEXTURE_SIZE.x) [[unlikely]]
			{
				uvOffset_ -= TEXTURE_SIZE.x;
			}

			UpdateTextureRect();
		}

	private:
		void UpdateTextureRect()
		{
			sprite_.setTextureRect(sf::IntRect(
				{ static_cast<i32>(uvOffset_), 0 },
				{ VISIBLE_TEXTURE_WIDTH, static_cast<i32>(TEXTURE_SIZE.y) }
			));
		}

		// Fly-weight approach (reused for no specific reason).
		static sf::Sprite MakeSprite()
		{
			static sf::Texture texture = [] {
				sf::Texture tex;
				if (!tex.loadFromFile(FLOOR_TEXTURE_PATH))
					throw std::runtime_error("Could not load FLOOR texture.");
				tex.setRepeated(true);
				return tex;
				}();
			return sf::Sprite(texture);
		}

		f32 uvOffset_ = 0.f;

		static constexpr sf::Vector2f TEXTURE_SIZE = { 336.f, 112.f };
		// All these values were found through iteration using imgui.
		static constexpr i32 VISIBLE_TEXTURE_WIDTH = 495; 
		static constexpr f32 FLOOR_Y = 626.f;             
		static constexpr f32 SPEED = -120.f;//-88.f;               
		static constexpr const char* FLOOR_TEXTURE_PATH = "Assets/base.png";
	};

	// Uses inherited sprite as center.
	class ObstaclePair : public Entity
	{
	public:
		static constexpr u32 MAX_VERTICAL_GAP = FlappyBirdGame::Bird::SPRITE_SIZE.y * 5.5f; // Vertical gap between Top and Bottom is 5.5 times the height of the bird. (This value was found analyzing the flappy bird game online.)
		static constexpr u32 MIN_VERTICAL_GAP = FlappyBirdGame::Bird::SPRITE_SIZE.y * 4; // Same as above.
		static constexpr u32 TUBE_TIP_HEIGHT = 24; // ...

		static constexpr f32 speedX = -120.f;
		sf::Vector2f topOffset = sf::Vector2f();
		sf::Vector2f bottomOfffset = sf::Vector2f();

		explicit ObstaclePair(const sf::Vector2f spawnPos) : Entity(MakeTempOrigin(), RenderOrder::OBSTACLES), top_(MakeSprite()), bottom_(MakeSprite())
		{
			// sprite_.setColor(sf::Color::Transparent); 
			sprite_.setPosition(spawnPos);
		//	sprite_.setScale({ 0.5f,0.5f }); // DESCALE TO make a smaller pivot and find center more easily.

			const auto centerOrigin = sf::Vector2f(top_.getLocalBounds().size.x / 2, top_.getLocalBounds().size.y / 2);
			top_.setOrigin(centerOrigin); // Both sprites use the same texture.
			bottom_.setOrigin(centerOrigin);
			
			top_.setRotation(sf::degrees(180.f));

			RandomizeSpritesY();
		}

		void Update(const f32 deltaTime) override
		{
			const auto currentPos = sprite_.getPosition();
			sprite_.move({ speedX * deltaTime, 0.f });
			top_.setPosition(currentPos + topOffset);
			bottom_.setPosition(currentPos + bottomOfffset);

			if (sprite_.getPosition().x + sprite_.getLocalBounds().size.x < 0)
			{
				sprite_.setPosition({ 480.f, sprite_.getPosition().y });
				RandomizeSpritesY();
			}
		}
		

		void PlaceBottomRandomY()
		{
			// SFML y coords... inverted...
			constexpr auto UPPER_POS = 110.f;
			constexpr auto LOWER_POS = 405.f;
			bottomOfffset.y = Random(UPPER_POS, LOWER_POS);
		}
		void PlaceTopRandomY()
		{
			// SFML y coords... inverted...
			constexpr auto UPPER_POS = -433.f;
			constexpr auto LOWER_POS = -140.f;
			topOffset.y = Random(UPPER_POS, LOWER_POS);
		}

		void RandomizeSpritesY()
		{
			do
			{
				PlaceBottomRandomY();
				PlaceTopRandomY();
			} while (!PipesInReasonableDistance());
		}

		// NOTE: Unfortunately because of the height of the pipes asset, the Top obstacle lowest point is -140, so in gameplay it doesnt look so random and gives the illusion that the obstacles are usually in the top zone of the screen.
		bool PipesInReasonableDistance() const
		{
			// Max gap distance is around 4 to 5.5 times the height of the bird.
			constexpr auto MIN_Y_GAP = FlappyBirdGame::Bird::SPRITE_SIZE.y * 4;
			constexpr auto MAX_Y_GAP = FlappyBirdGame::Bird::SPRITE_SIZE.y * 5.5f;
			const f32 tubeHalfHeight = bottom_.getLocalBounds().size.y / 2.f;

			// Offsets, not sprite positions: positions only refresh in Update().
			const f32 topTipY = topOffset.y + tubeHalfHeight;          // lowest point of the top tube
			const f32 bottomTipY = bottomOfffset.y - tubeHalfHeight;   // highest point of the bottom tube

			const f32 gap = bottomTipY - topTipY;
			
			return gap >= MIN_Y_GAP && gap <= MAX_Y_GAP;
		}

		inline sf::Sprite& GetTopSprite() { return top_; }
		inline sf::Sprite& GetBottomSprite() { return bottom_; }

	private:
		// For faster iteration. TODO: remove.
		static sf::Sprite MakeTempOrigin()
		{
			static sf::Texture texture = [] {
				sf::Texture tex;
				constexpr auto TEMP_TEXTURE_PATH = "Assets/bluebird-midflap.png";
				if (!tex.loadFromFile(TEMP_TEXTURE_PATH))
					throw std::runtime_error("Could not load TEMP ORIGIN texture.");
				return tex;
				}();
			return sf::Sprite(texture);
		}


		// Fly-weight approach is very useful in this case since there are 3 instances of the obstacle.
		static sf::Sprite MakeSprite()
		{
			static sf::Texture texture = [] {
				sf::Texture tex;
				if (!tex.loadFromFile(TEXTURE_PATH))
					throw std::runtime_error("Could not load OBSTACLE texture.");
				return tex;
				}();
			return sf::Sprite(texture);
		}

		sf::Sprite top_;
		sf::Sprite bottom_;


		static constexpr const char* TEXTURE_PATH = "Assets/pipe-green.png";
		static constexpr sf::Vector2f TEXTURE_SIZE = { 52.f, 320.f };
	};

	class World : public ::World
	{
	public:
		World(sf::RenderWindow& window) : ::World(window)
		{
			// I do not think it is necessary for this small game to implement an Init() approach.


			entities_.reserve(static_cast<size_t>(8)); // Bird, floor, and 3 pairs of obstacles.
			
			// Create bird.
			bird_ = Add<Bird>();
			assert(bird_);
			assert(entities_[0].get() == bird_);

			bird_->GetSprite().setPosition(
				{static_cast<f32>(window_.getSize().x) / 3, static_cast<f32>(window.getSize().y) / 2 }
			);

			// Create Floor.
			floor_ = Add<Floor>();
			
			// Create obstacles.
			sf::Vector2f windowBorder = sf::Vector2f(window_.getSize()) / 2.f; // Right border, center of the window.
			windowBorder.x = window.getSize().x;
			obstTest1_ = Add<ObstaclePair>(windowBorder);
			obstaclePairs_[0] = obstTest1_;
			obstaclePairs_[1] = nullptr;
			obstaclePairs_[2] = nullptr;

			
			// Add sprites to draw order pipeline.
			AddAdditionalSprite(&obstTest1_->GetTopSprite(), FlappyBirdGame::RenderOrder::OBSTACLES);
			AddAdditionalSprite(&obstTest1_->GetBottomSprite(), FlappyBirdGame::RenderOrder::OBSTACLES);
		}

		void Update(const f32 deltaTime) override
		{
			::World::Update(deltaTime);
			// TODO: check collisions.


			// Score system. TODO: should be placed in a function.
			for (const auto& obstPair : obstaclePairs_)
			{
				if (!obstPair) continue;

				if (obstPair->GetSprite().getPosition().x < bird_->GetSprite().getPosition().x && !scored_)
				{
					score_++;
					scored_ = true;
					std::cout << "SCORE: " << score_ << std::endl;
				}
				if (obstPair->GetSprite().getPosition().x > window_.getSize().x / 2)
				{
					scored_ = false;
				}
			}
		}

		void HandleInput(const std::optional<sf::Event>& event) override
		{
			if (const auto* mouseClic = event->getIf<sf::Event::MouseButtonPressed>())
			{
				if (mouseClic->button == sf::Mouse::Button::Left)
				{
					bird_->Flap();
				}
			}
		}

		void OnImGuiUpdateValues() override
		{
			ImGui::DragFloat2("top offset:", &obstTest1_->topOffset.x, 0.1f);
			ImGui::DragFloat2("bottom offset:", &obstTest1_->bottomOfffset.x, 0.1f);
			
			if (ImGui::Button("Randomize Y", ImGui::CalcTextSize("pivot new random Y")) && obstTest1_)
			{
				obstTest1_->RandomizeSpritesY();
				std::cout << "clicked2" << std::endl;
			}
		}

	//	void CheckCollisions(); // TODO:
	private:
		static constexpr i32 NUM_OBSTACLE_PAIRS = 3;
		Bird* bird_;
		Floor* floor_;
		ObstaclePair* obstTest1_;
		std::array<ObstaclePair*, NUM_OBSTACLE_PAIRS> obstaclePairs_ = {};
		u32 score_ = 0;
		bool scored_ = false;// temp TODO:
	};
}


int main()
{
	sf::RenderWindow window( sf::VideoMode( { 480, 600} ), // Original Flappy bird size according to google: 288*512.
		"SFML works!",
		sf::Style::Titlebar | sf::Style::Close // No resizing.
		);
	window.setFramerateLimit(60U);
	
	App<FlappyBirdGame::World> app{ window };
	
	app.Run();

	return 0;
}

#include "../minui.hpp"

using namespace minui;

const uint8_t logoLightBmpData[] =
#include "logo.light.bmp.data"
;

const uint8_t logoDarkBmpData[] =
#include "logo.dark.bmp.data"
;

int main()
{
	Application::initialize();

	// custom styles
	enum MyStyles
	{
		TitleLabel = Styles::Custom + 1,
		ColorLabel,
		ColorButton,
	};


	Window window;
	window.create();
	window.setSize(600, 450);
	window.setTitle("minui example");
	window.setOnClose([&]()
		{
			window.close();
			Application::quit();
		}
	);

	Label title;
	title.setRect({ 0, 60, 600, 40 });
	title.setId(MyStyles::TitleLabel);
	title.setText("MinUI Example");
	window.addWidget(&title);

	Label label1;
	label1.setRect(Rect{ 10, title.rect().y + title.rect().height, title.rect().width, 30});
	label1.setText("Minimize Direct-UI with one CPP header!");
	window.addWidget(&label1);

	Label label2;
	label2.setRect(Rect{ 10, label1.rect().y + label1.rect().height, label1.rect().width, 30});
	label2.setText("It still supports Anti-Aliasing HiDPI and Dark-Mode :)");
	window.addWidget(&label2);

	Label label3;
	label3.setRect(Rect{ label2.rect().x, label2.rect().y + label2.rect().height + 10, label2.rect().width, 30});
	label3.setText("Custom style are supported.");
	label3.setId(MyStyles::ColorLabel);
	window.addWidget(&label3);

	Button button1;
	button1.setRect(Rect{ label3.rect().x, label3.rect().y + label3.rect().height + 10, 80, 30});
	button1.setText("Button");
	window.addWidget(&button1);

	Button button2;
	button2.setRect(Rect{ button1.rect().x + button1.rect().width + 10, button1.rect().y, 80, 30 });
	button2.setId(MyStyles::ColorButton);
	button2.setText("Styled");
	window.addWidget(&button2);

	Button button3;
	button3.setRect(Rect{ button2.rect().x + button2.rect().width + 10, button2.rect().y, 80, 30 });
	window.addWidget(&button3);

	bool dark = false;
	bool autoDark = false;
	button3.setText(dark ? "Light" : "Dark");
	std::function<void(bool)> setDarkStyles;
	button3.setOnClick([&]()
		{
			autoDark = false;
			dark = !dark;
			setDarkStyles(dark);
			button3.setText(dark ? "Light" : "Dark");
		}
	);

	Button button4;
	button4.setRect(Rect{ button3.rect().x + button3.rect().width + 10, button3.rect().y, 100, 30 });
	button4.setText("Auto Dark");
	button4.setOnClick([&]()
		{
			autoDark = true;
		}
	);
	window.addWidget(&button4);

	Button button5;
	button5.setRect(Rect{ 10, button4.rect().y + button4.rect().height + 10, 120, 40 });
	window.addWidget(&button5);

	Progress progress;
	progress.setRect(Rect{ button5.rect().x + button5.rect().width + 10, button5.rect().y + 15, 400, 10 });
	window.addWidget(&progress);

	int step = 0;
	window.addTimer(500, [&]()
		{
			step += 10;
			if (step > 100)
				step = 0;
			progress.setStep(float(step) / 100.0);
			return false;
		}
	);

	button5.setText(progress.visible() ? "Hide Progress" : "Show Progress");
	button5.setOnClick([&]()
		{
			progress.setVisible(!progress.visible());
			button5.setText(progress.visible() ? "Hide Progress" : "Show Progress");
		}
	);

	Label label4;
	label4.setRect(Rect{ 10, button5.rect().y + button5.rect().height + 10, 50, 30 });
	label4.setText("Image:");
	window.addWidget(&label4);

	Image logo1;
	logo1.setRect(Rect{ label4.rect().x + label4.rect().width + 10, label4.rect().y, 32, 32 });
	window.addWidget(&logo1);

	Image logo2;
	logo2.setRect(Rect{ logo1.rect().x + logo1.rect().width + 10, logo1.rect().y, 64, 64 });
	window.addWidget(&logo2);

	Image logo3;
	logo3.setRect(Rect{ logo2.rect().x + logo2.rect().width + 10, logo2.rect().y, 128, 128 });
	window.addWidget(&logo3);

	setDarkStyles = [&](bool darkMode)
		{
			Application::setStyles(darkMode);

			auto& styles = Styles::instance();
			auto style = styles.getStyle(Styles::Label);
			style.fontSize = 32;
			styles.setStyle(MyStyles::TitleLabel, style);

			style = styles.getStyle(Styles::Label);
			style.color = Color{ 0, 180, 0 };
			styles.setStyle(MyStyles::ColorLabel, style);

			style = styles.getStyle(Styles::Button);
			style.backgroundColor = Color{ 53, 132, 220 };
			styles.setStyle(MyStyles::ColorButton, style);
			style.backgroundColor = Color{ 73, 140, 230 };
			styles.setStyle(MyStyles::ColorButton + Button::Hover, style);
			style.backgroundColor = Color{ 42, 106, 183 };
			styles.setStyle(MyStyles::ColorButton + Button::Press, style);

			dark = darkMode;
			button3.setText(dark ? "Light" : "Dark");

			auto logoData = darkMode ? logoDarkBmpData : logoLightBmpData;
			logo1.setBmpData(logoData);
			logo2.setBmpData(logoData);
			logo3.setBmpData(logoData);

			window.update();
		};

	setDarkStyles(dark);

	// auto set dark
	window.addTimer(1000, [&]()
		{
			if (autoDark)
			{
				bool darkMode = Application::isDarkMode(); // timed check dark mode
				setDarkStyles(darkMode);
			}
			return false;
		}
	); 

	window.show();
	Application::exec();
	return 0;
}

#ifdef WIN32
int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int CmdShow) {
	return main();
}
#endif
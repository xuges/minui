#ifdef WIN32
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>

#include <map>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

#include <stdio.h>

namespace minui
{
	namespace utils
	{
		inline std::wstring utf8ToUtf16(const char* src)
		{
			int length = MultiByteToWideChar(CP_UTF8, 0, src, -1, NULL, 0);
			std::wstring dst;
			dst.resize(length-1);
			MultiByteToWideChar(CP_UTF8, 0, src, -1, &dst[0], length);
			return dst;
		}

		template <size_t N>
		inline void utf8ToUtf16(const char* src, TCHAR (&dst)[N])
		{
			MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, N);
		}
	}

	class Handle
	{
	public:
		Handle(const Handle&) = delete;
		Handle& operator=(const Handle&) = delete;
		Handle(Handle&&) = delete;
		Handle& operator=(Handle&&) = delete;

	protected:
		Handle() = default;
		~Handle() = default;
	};

	struct Color
	{
		uint8_t r;
		uint8_t g;
		uint8_t b;
		uint8_t _;

		COLORREF toColorRef() const
		{
			return RGB(r, g, b);
		}
	};

	struct Point
	{
		int x;
		int y;
	};

	struct Rect
	{
		int x;
		int y;
		int width;
		int height;

		bool contains(Point pt) const
		{
			return x <= pt.x && pt.x <= x + width && y <= pt.y && pt.y < y + height;
		}

		RECT toRect() const
		{
			return { x, y, x + width, y + height };
		}
	};

	struct Style
	{
		Color color;
		Color backgroundColor;
		int radius;
		int fontSize;
		const char* fontFamily[6];

		static const Style& defaultStyle()
		{
			static Style style =
			{
				Color{50, 50, 50},
				Color{250, 250, 251},
				6,
				18,
				{"Microsoft YaHei UI", "SimSun", "sans-seirf", "sans", "Ariel", NULL}
			};
			return style;
		}
	};

	class Styles : public Handle
	{
	public:
		static Styles& instance()
		{
			static Styles styles;
			return styles;
		}

		void addStyle(const std::string& sel, const Style& style)
		{
			styles_[sel].push_back(style);
		}

		const Style& getStyle(const std::string& sel, const std::string& fallback = "window") const
		{
			auto iter = styles_.find(sel);
			if (iter != styles_.end())
				return iter->second.back();

			iter = styles_.find(fallback);
			if (iter != styles_.end())
				return iter->second.back();

			return Style::defaultStyle();
		}

	private:
		std::map<std::string, std::vector<Style> > styles_;
	};

	class Painter : public Handle
	{
	public:
		void drawPixel(int x, int y, Color color)
		{
			SetPixel(mem_, x, y, color.toColorRef());
		}

		void drawLine(int x, int y, int x1, int y1, int width, Color color)
		{
			HPEN pen = CreatePen(PS_SOLID, width, color.toColorRef());
			HPEN old = (HPEN)SelectObject(mem_, pen);

			MoveToEx(mem_, x, y, NULL);
			LineTo(mem_, x1, y1);

			SelectObject(mem_, old);
			DeleteObject(pen);
		}

		void drawText(const Rect& rect, const char* text, const Style& style)
		{
			HFONT oldFont;
			HFONT font = createFont(style);
			if (font)
				oldFont = (HFONT)SelectObject(mem_, font);

			SetBkMode(mem_, TRANSPARENT);
			auto buf = utils::utf8ToUtf16(text);
			auto drawRect = rect.toRect();
			auto oldColor = SetTextColor(mem_, style.color.toColorRef());
			DrawText(mem_, buf.c_str(), buf.length(), &drawRect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
			SetTextColor(mem_, oldColor);

			if (font)
				SelectObject(mem_, oldFont);
		}

		void drawImage(const Rect& rect, const uint8_t* bmp, size_t len)
		{
			if (bmp[0] != 0x42 && bmp[1] != 0x4d)
				return;

			BITMAPFILEHEADER* bfh = (BITMAPFILEHEADER*)bmp;
			if (bfh->bfSize > len)
				return;

			BITMAPINFOHEADER* bih = (BITMAPINFOHEADER*)(bmp + sizeof(BITMAPFILEHEADER));
			if (bih->biBitCount < 24)
				return;

			int width = bih->biWidth;
			int height = bih->biHeight;
			const uint8_t* pixels = bmp + bfh->bfOffBits;

			HDC dc = CreateCompatibleDC(hdc_);
			HBITMAP bitmap = CreateCompatibleBitmap(hdc_, width, height);
			HBITMAP old = (HBITMAP)SelectObject(dc, bitmap);
			BITMAPINFO bi = { 0 };
			bi.bmiHeader = *bih;
			SetDIBits(dc, bitmap, 0, height, pixels, &bi, DIB_RGB_COLORS);
			BitBlt(mem_, rect.x, rect.y, rect.x + rect.width, rect.y + rect.height, dc, 0, 0, SRCCOPY);
			SelectObject(dc, old);
			DeleteObject(bitmap);
			DeleteDC(dc);
		}

		void frameRect(const Rect& rect, int lineWidth, Color color)
		{
			RECT frame = rect.toRect();
			HBRUSH brush = CreateSolidBrush(color.toColorRef());
			FrameRect(mem_, &frame, brush);
			DeleteObject(brush);
		}

		void fillRect(const Rect& rect, Color color)
		{
			RECT fill = rect.toRect();
			HBRUSH brush = CreateSolidBrush(color.toColorRef());
			FillRect(mem_, &fill, brush);
			DeleteObject(brush);
		}

		void fillRoundRect(const Rect& rect, int radius, Color color)
		{
			HBRUSH brush = CreateSolidBrush(color.toColorRef());
			HRGN hrgn = CreateRoundRectRgn(rect.x, rect.y, rect.x + rect.width, rect.y + rect.height, radius, radius);
			FillRgn(mem_, hrgn, brush);
			DeleteObject(hrgn);
			DeleteObject(brush);			
		}

		void roundRect(const Rect& rect, int lineWidth, int radius, Color color)
		{
			HBRUSH brush = CreateSolidBrush(color.toColorRef());
			HRGN hrgn = CreateRoundRectRgn(rect.x, rect.y, rect.x + rect.width, rect.y + rect.height, radius, radius);
			FrameRgn(mem_, hrgn, brush, lineWidth, lineWidth);
			DeleteObject(hrgn);
			DeleteObject(brush);
		}

	private:
		friend class Window;

		Painter(HDC hdc, int width, int height)
			: hdc_(hdc)
			, width_(width)
			, height_(height)
		{
			mem_ = CreateCompatibleDC(hdc);
			bitmap_ = CreateCompatibleBitmap(hdc, width, height);
			old_ = (HBITMAP)SelectObject(mem_, bitmap_);
		}

		~Painter()
		{
			BitBlt(hdc_, 0, 0, width_, height_, mem_, 0, 0, SRCCOPY);
			SelectObject(mem_, old_);
			DeleteDC(mem_);
			DeleteObject(bitmap_);
		}

		void setClipRect(const Rect& rect)
		{
			RECT rt = rect.toRect();
			HRGN hrgn = CreateRectRgnIndirect(&rt);
			SelectClipRgn(mem_, hrgn);
			DeleteObject(hrgn);
		}

		HFONT createFont(const Style& style)
		{
			if (style.fontFamily)
			{
				LOGFONT ft = { 0 };
				ft.lfHeight = style.fontSize;
				for (auto fontFamily : style.fontFamily)
				{
					if (fontFamily)
					{
						utils::utf8ToUtf16(fontFamily, ft.lfFaceName);
						HFONT font = CreateFontIndirect(&ft);
						if (font)
							return font;
					}
				}
			}
			return NULL;
		}

	private:
		HDC hdc_;
		HDC mem_;
		HBITMAP bitmap_;
		HBITMAP old_;
		int width_;
		int height_;
	};

	class Window;

	class Widget : public Handle
	{
	public:
		using OnDrawFunc = std::function<void(Painter&)>;

		const std::string& name() const
		{
			return name_;
		}

		void setName(const std::string& name)
		{
			name_ = name;
		}

		const Rect& rect() const
		{
			return rect_;
		}

		void setRect(const Rect& rect)
		{
			rect_ = rect;
		}

		bool visible() const
		{
			return visible_;
		}

		void setVisible(bool v)
		{
			visible_ = v;
		}

		void update();

	protected:
		Widget()
			: window_(nullptr)
			, rect_{ 0 }
			, visible_(true)
		{}

		virtual void draw(Painter& painter) {}
		virtual void mouseMove(bool leave) {}
		virtual void mouseButton(bool press) {}

	private:
		friend class Window;
		void setWindow(Window* win)
		{
			window_ = win;
		}

		void setOnDraw(const OnDrawFunc& fn)
		{
			onDraw_ = fn;
		}

		void onDraw(Painter& painter)
		{
			if (visible_)
			{
				draw(painter);
				if (onDraw_)
					onDraw_(painter);
			}
		}

	private:
		std::string name_;
		Rect rect_;
		Style style_;
		OnDrawFunc onDraw_;
		Window* window_;
		bool visible_;
	};

	class Button;

	class Window : public Handle
	{
	public:
		using TimerFunc = std::function<bool()>;
		using OnCloseFunc = std::function<void()>;

		static Window* create()
		{
			int style = WS_OVERLAPPED | WS_CAPTION | WS_THICKFRAME;
			HWND hwnd = CreateWindowEx(0, WndClass, L"Window", style, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, NULL, NULL);
			if (!hwnd || hwnd == INVALID_HANDLE_VALUE)
				return nullptr;

			MARGINS margin = { 1,1,1,1 };
			::DwmExtendFrameIntoClientArea(hwnd, &margin);

			auto win = new Window();
			win->hwnd_ = hwnd;

			SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)win);
			return win;
		}

		const std::string& title() const
		{
			return title_;
		}

		void setTitle(const std::string& text)
		{
			title_ = text;
			auto titleText = utils::utf8ToUtf16(text.c_str());
			SetWindowText(hwnd_, titleText.c_str());
		}

		void setSize(int width, int height)
		{
			SetWindowPos(hwnd_, NULL, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER);
		}

		void addWidget(Widget* w)
		{
			widgets_.push_back(w);
			w->setWindow(this);
		}


		void addTimer(int msec, const TimerFunc& fn)
		{
			timers_.insert({ timerId_, fn });
			::SetTimer(hwnd_, timerId_, msec, NULL);
			timerId_++;
		}

		void show();

		void update()
		{
			InvalidateRect(hwnd_, NULL, FALSE);
		}

		void close()
		{
			CloseWindow(hwnd_);
		}

		void setOnClose(const OnCloseFunc fn)
		{
			onClose_ = fn;
		}

		void setCloseable(bool v);

	private:
		Window()
			: hwnd_(NULL)
			, title_({0})
			, close_(nullptr)
			, timerId_(0)
			, mouseWidget_(nullptr)
			, mouseIn_(false)
		{

		}

		friend class Application;

		static constexpr LPCTSTR WndClass = L"minuiWindow";

		static LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
		{
			auto window = (Window*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
			if (!window)
				return DefWindowProc(hwnd, msg, wParam, lParam);

			switch (msg)
			{
			case WM_NCCALCSIZE:
				return 0;

			case WM_NCHITTEST:
			{
				POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				ScreenToClient(hwnd, &pt);

				Point point = { pt.x, pt.y };
				window->onMouseMove(point, false);

				if (window->onTestTitle(point))
					return HTCAPTION;
			
				break; // default
			}

			case WM_TIMER:
			{
				if (window->onTimer(wParam))
					KillTimer(hwnd, wParam);
				break;
			}
			case WM_CLOSE:
				window->onClose();
				return 0;

			case WM_PAINT:
			{
				PAINTSTRUCT ps;
				HDC hdc = BeginPaint(hwnd, &ps);
				RECT rect;
				GetClientRect(hwnd, &rect);
				window->onPaint(hdc, rect.right - rect.left, rect.bottom - rect.top);
				EndPaint(hwnd, &ps);
				return 0;
			}

			case WM_MOUSELEAVE:
			case WM_MOUSEMOVE:
			{
				POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				window->onMouseMove(Point{ pt.x, pt.y }, msg == WM_MOUSELEAVE);
				return 0;
			}
			case WM_LBUTTONDOWN:
			case WM_LBUTTONUP:
				window->onMouseButton(msg == WM_LBUTTONDOWN);
				return 0;
			}

			return DefWindowProc(hwnd, msg, wParam, lParam);
		}

		static bool trackMouseEvent(HWND hwnd)
		{
			TRACKMOUSEEVENT tme;
			tme.cbSize = sizeof(TRACKMOUSEEVENT);
			tme.dwFlags = TME_LEAVE;
			tme.hwndTrack = hwnd;
			return TrackMouseEvent(&tme);
		}

		void onClose()
		{
			if (onClose_)
				onClose_();
		}

		bool onTimer(int id)
		{
			auto iter = timers_.find(id);
			if (iter != timers_.end())
				return iter->second();
			return false;
		}

		void onPaint(HDC hdc, int width, int height)
		{
			Rect rect = { 0,0,width, height };
			auto style = Styles::instance().getStyle("window");

			Painter painter(hdc, width, height);
			painter.frameRect(rect, 1, Color{ 130,130,130 });
			painter.fillRect(Rect{0, 0, width, height}, style.backgroundColor);

			for (auto widget : widgets_)
			{
				painter.setClipRect(widget->rect());
				widget->onDraw(painter);
			}
		}

		void onMouseMove(Point pt, bool leave)
		{
			if (leave && mouseWidget_)
			{
				mouseWidget_->mouseMove(true);  // mouse leave
				mouseWidget_ = nullptr;
				mouseIn_ = false;
				return;
			}

			if (!mouseIn_)
				mouseIn_ = trackMouseEvent(hwnd_);

			for (auto iter = widgets_.rbegin(); iter != widgets_.rend(); iter++)
			{
				Widget* widget = *iter;
				if (widget->visible() && widget->rect().contains(pt))
				{
					if (mouseWidget_ && widget != mouseWidget_)
						mouseWidget_->mouseMove(true); // mouse leave
					widget->mouseMove(false);
					mouseWidget_ = widget;
					return;
				}
			}

			if (mouseWidget_)
			{
				mouseWidget_->mouseMove(true); // mouse leave
				mouseWidget_ = nullptr;
			}
		}

		void onMouseButton(bool press)
		{
			if (mouseWidget_)
				mouseWidget_->mouseButton(press);
		}

		bool onTestTitle(Point pt)
		{
			return titleRect_.contains(pt);
		}

		HWND hwnd_;
		std::string title_;
		Rect titleRect_;
		Button* close_;
		int timerId_;
		OnCloseFunc onClose_;
		std::map<int, TimerFunc> timers_;
		std::vector<Widget*> widgets_;
		Widget* mouseWidget_;
		bool mouseIn_;

	};

	inline void Widget::update()
	{
		if (window_ && visible_)
			window_->update();
	}

	class Application : public Handle
	{
	public:
		static bool initialize()
		{
			WNDCLASSEX wcx = { 0 };
			wcx.cbSize = sizeof(WNDCLASSEX);
			wcx.style = CS_HREDRAW | CS_VREDRAW;
			wcx.lpfnWndProc = Window::WndProc;
			wcx.cbClsExtra = 0;
			wcx.cbWndExtra = 0;
			wcx.hInstance = NULL;
			wcx.hCursor = LoadCursor(NULL, IDC_ARROW);
			wcx.hbrBackground = (HBRUSH)COLOR_WINDOWFRAME;
			wcx.lpszClassName = Window::WndClass;
			RegisterClassEx(&wcx);
			int err = GetLastError();

			Styles& styles = Styles::instance();

			auto style = Style::defaultStyle();
			styles.addStyle("window", style);
			styles.addStyle("label", style);
			styles.addStyle("image", style);

			style.backgroundColor = Color{ 230, 230, 230 };
			styles.addStyle("button", style);

			style.backgroundColor = Color{ 220,220,221 };
			styles.addStyle("button:hover", style);

			style.backgroundColor = Color{ 190,190,192 };
			styles.addStyle("button:press", style);

			style.color = Color{ 53,132,228 };
			style.backgroundColor = Color{ 235,232,230 };
			styles.addStyle("progress", style);

			return err == 0;
		}

		static void exec()
		{
			MSG msg;
			while (GetMessage(&msg, NULL, 0, 0))
			{
				::TranslateMessage(&msg);
				::DispatchMessage(&msg);
			}
		}

		static void quit()
		{
			PostQuitMessage(0);
		}
	};


	class Label : public Widget
	{
	public:
		static Label* create()
		{
			return new Label();
		}

		const std::string& text() const
		{
			return text_;
		}

		void setText(const std::string& text)
		{
			text_ = text;
		}

	protected:
		Label()
		{
			setName("label");		
		}

		void draw(Painter& painter) override
		{
			auto style = Styles::instance().getStyle(name(), "label");
			painter.drawText(rect(), text_.c_str(), style);
		}

	private:
		std::string text_;
	};


	class Button : public Widget
	{
	public:
		enum State
		{
			Normal,
			Hover,
			Press
		};

		static const std::string& stateStyle(State state)
		{
			static std::string strings[] = { "", ":hover", ":press" };
			return strings[state];
		}

		using OnClickFunc = std::function<void()>;

		static Button* create()
		{
			return new Button();
		}

		State state() const
		{
			return state_;
		}

		const std::string& text() const
		{
			return text_.c_str();
		}

		void setText(const std::string& text)
		{
			text_ = text;
		}

		void setOnClick(const OnClickFunc& fn)
		{
			onClick_ = fn;
		}

	protected:
		Button()
			: state_(Normal)
		{
			setName("button");
		}

		void draw(Painter& painter) override
		{
			auto style = Styles::instance().getStyle(name() + stateStyle(state_), "button" + stateStyle(state_));
			painter.fillRoundRect(rect(), style.radius, style.backgroundColor);
			if (text_.size())
				painter.drawText(rect(), text_.c_str(), style);
		}

		void mouseMove(bool leave) override
		{
			state_ = leave ? Normal : Hover;
			update();
		}

		void mouseButton(bool press) override
		{
			if (press)
			{
				state_ = Press;
			}
			else
			{
				state_ = Hover;
				if (onClick_)
					onClick_();
			}

			update();
		}

	private:
		std::string text_;
		State state_;
		Style styles_[3];
		OnClickFunc onClick_;
	};

	class Progress : public Widget
	{
	public:
		static Progress* create()
		{
			return new Progress();
		}

		void setStep(float step)
		{
			if (0 <= step && step <= 1.0)
			{
				step_ = step;
				update();
			}
		}

	protected:
		Progress()
			: step_(0)
		{
			setName("progress");
		}

		void draw(Painter& painter) override
		{
			auto style = Styles::instance().getStyle(name(), "progress");
			painter.fillRoundRect(rect(), style.radius, style.backgroundColor);

			if (0 < step_)
			{
				Rect stepRect = rect();
				stepRect.width *= step_;
				painter.fillRoundRect(stepRect, style.radius, style.color);
			}
		}

	private:
		float step_;
	};

	class Image : public Widget
	{
	public:
		static Image* create()
		{
			return new Image();
		}

		void setBmpData(const uint8_t* data, size_t len)
		{
			bmp_ = data;
			bmpSize_ = len;
		}

	protected:
		Image()
			: bmp_(nullptr)
			, bmpSize_(0)
		{}

		void draw(Painter& painter) override
		{
			if (bmp_)
				painter.drawImage(rect(), bmp_, bmpSize_);
		}

	private:
		const uint8_t* bmp_;
		size_t bmpSize_;
	};

	inline void Window::show()
	{
		UpdateWindow(hwnd_);
		ShowWindow(hwnd_, SW_NORMAL);

		RECT rect;
		GetClientRect(hwnd_, &rect);

		int width = rect.right - rect.left;
		int height = rect.bottom - rect.top;

		titleRect_ = Rect{ 0, 0, width - 48, 32 };

		auto& styles = Styles::instance();
		auto style = styles.getStyle("window");
		style.radius = 0;
		styles.addStyle("CloseButton", style);

		style.backgroundColor = Color{ 196,43,28 };
		styles.addStyle("CloseButton:hover", style);

		style.backgroundColor = Color{ 181,43,30 };
		styles.addStyle("CloseButton:press", style);

		close_ = Button::create();
		close_->setName("CloseButton");
		close_->setRect(Rect{ titleRect_.width, 0,  48, 32 });
		close_->setOnDraw([=](Painter& painter)
			{
				Rect rect = close_->rect();
				// draw 12 x 12  x
				int xCenter = rect.x + rect.width / 2;
				int yCenter = rect.y + rect.height / 2;
				int xWidth = 12;
				int xHeight = 12;
				painter.drawLine(xCenter, yCenter, xCenter - 6, yCenter + 6, 1, style.color);
				painter.drawLine(xCenter, yCenter, xCenter + 6, yCenter + 6, 1, style.color);
				painter.drawLine(xCenter, yCenter, xCenter + 6, yCenter - 6, 1, style.color);
				painter.drawLine(xCenter, yCenter, xCenter - 6, yCenter - 6, 1, style.color);
			});
		close_->setOnClick([=]
			{
				this->close();
				Application::quit();
			});

		addWidget(close_);
	}

	void Window::setCloseable(bool v)
	{
		close_->setVisible(v);
	}
}

#endif

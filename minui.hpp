#ifdef WIN32
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>

#include <functional>
#include <cstdint>

namespace minui
{
	namespace utils
	{
		struct WString
		{
			wchar_t* data;
			size_t length;

			~WString()
			{
				delete[] data;
			}
		};

		inline WString utf8ToUtf16(const char* src)
		{
			int length = MultiByteToWideChar(CP_UTF8, 0, src, -1, NULL, 0);
			auto data = new wchar_t[length];
			data[length - 1] = 0;
			MultiByteToWideChar(CP_UTF8, 0, src, -1, data, length - 1);
			return WString{ data, size_t(length - 1) };
		}

		inline void utf8ToUtf16(const char* src, wchar_t* dst, size_t len)
		{
			MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, len);
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

		RECT toRect(int scale = 1) const
		{
			return { x * scale, y * scale, x * scale + width * scale, y * scale + height * scale };
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
		enum Id
		{
			Window,
			Button,
			ButtonHover,
			ButtonPress,
			Label,
			Image,
			Progress,
			CloseButton,
			CloseButtonHover,
			CloseButtonPress,
			Custom,
			Count = 128
		};

		static Styles& instance()
		{
			static Styles styles;
			return styles;
		}

		void setStyle(int id, const Style& style)
		{
			if (0 <= id && id < Count)
				styles_[id] = style;
		}

		const Style& getStyle(int id) const
		{
			if (0 <= id && id < Count)
				return styles_[id];
			return styles_[0];
		}

	private:
		Styles()
		{
			for (size_t i = 0; i < Count; ++i)
				styles_[i] = Style::defaultStyle();
		}

		Style styles_[Count];
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
			DrawText(mem_, buf.data, buf.length, &drawRect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
			SetTextColor(mem_, oldColor);

			if (font)
				SelectObject(mem_, oldFont);
		}

		void drawImage(const Rect& rect, const uint8_t* bmp)
		{
			if (bmp[0] != 0x42 && bmp[1] != 0x4d)
				return;

			BITMAPFILEHEADER* bfh = (BITMAPFILEHEADER*)bmp;
			/*if (bfh->bfSize > len)
				return;*/

			BITMAPINFOHEADER* bih = (BITMAPINFOHEADER*)(bmp + sizeof(BITMAPFILEHEADER));
			if (bih->biBitCount < 24)
				return;

			int width = bih->biWidth;
			int height = bih->biHeight;
			const uint8_t* pixels = bmp + bfh->bfOffBits;

			HDC dc = CreateCompatibleDC(mem_);
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
						utils::utf8ToUtf16(fontFamily, ft.lfFaceName, sizeof(ft.lfFaceName) - 1);
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

		virtual ~Widget() = default;

		int id() const
		{
			return id_;
		}

		void setId(int id)
		{
			id_ = id;
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
		Rect rect_;
		Style style_;
		OnDrawFunc onDraw_;
		Window* window_;
		int id_;
		bool visible_;
	};

	class Button;

	class Window : public Handle
	{
	public:
		enum
		{
			TimerCount = 32,
			WidgetCount = 64
		};

		using TimerFunc = std::function<bool()>;
		using OnCloseFunc = std::function<void()>;

		Window()
			: hwnd_(NULL)
			, title_(nullptr)
			, close_(nullptr)
			, timerId_(0)
			, widgetIndex_(0)
			, mouseWidget_(nullptr)
			, mouseIn_(false)
		{

		}

		~Window()
		{
			if (hwnd_)
				DestroyWindow(hwnd_);
		}

		bool create()
		{
			int style = WS_OVERLAPPED | WS_CAPTION | WS_THICKFRAME;
			HWND hwnd = CreateWindowEx(0, WndClass, L"Window", style, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, NULL, NULL);
			if (!hwnd || hwnd == INVALID_HANDLE_VALUE)
				return false;

			MARGINS margin = { 1,1,1,1 };
			::DwmExtendFrameIntoClientArea(hwnd, &margin);

			hwnd_ = hwnd;
			SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)this);
			return true;
		}

		const char* title() const
		{
			return title_;
		}

		void setTitle(const char* text)
		{
			title_ = text;
			auto titleText = utils::utf8ToUtf16(text);
			SetWindowText(hwnd_, titleText.data);
		}

		void setSize(int width, int height)
		{
			SetWindowPos(hwnd_, NULL, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER);
		}

		bool addWidget(Widget* w)
		{
			if (widgetIndex_ < WidgetCount)
			{
				widgets_[widgetIndex_++] = w;
				w->setWindow(this);
				return true;
			}
			return false;
		}

		bool addTimer(int msec, const TimerFunc& fn)
		{
			if (timerId_ < TimerCount)
			{
				timers_[timerId_] = fn;
				::SetTimer(hwnd_, timerId_, msec, NULL);
				timerId_++;
				return true;
			}
			return false;
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

			case WM_DPICHANGED:
			{
				int dpi = HIWORD(wParam);
				printf("DPI changed: %d\n", dpi);

				RECT* rect = (RECT*)lParam;
				SetWindowPos(hwnd, NULL, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
				return 0;
			}
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
			return timers_[id]();
		}

		void onPaint(HDC hdc, int width, int height)
		{
			Rect rect = { 0,0,width, height };
			auto style = Styles::instance().getStyle(Styles::Window);

			Painter painter(hdc, width, height);
			painter.frameRect(rect, 1, Color{ 130,130,130 });
			painter.fillRect(Rect{ 0, 0, width, height }, style.backgroundColor);

			for (int i = 0; i < widgetIndex_; ++i)
			{
				Widget* widget = widgets_[i];
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

			for (int i = widgetIndex_ - 1; i >= 0; --i)
			{
				Widget* widget = widgets_[i];
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
		const char* title_;
		Rect titleRect_;
		Button* close_;
		int timerId_;
		int widgetIndex_;
		OnCloseFunc onClose_;
		TimerFunc timers_[TimerCount];
		Widget* widgets_[WidgetCount];
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
			styles.setStyle(Styles::Window, style);
			styles.setStyle(Styles::Label, style);
			styles.setStyle(Styles::Image, style);

			style.backgroundColor = Color{ 230, 230, 230 };
			styles.setStyle(Styles::Button, style);

			style.backgroundColor = Color{ 220,220,221 };
			styles.setStyle(Styles::ButtonHover, style);

			style.backgroundColor = Color{ 190,190,192 };
			styles.setStyle(Styles::ButtonPress, style);

			style.color = Color{ 53,132,228 };
			style.backgroundColor = Color{ 235,232,230 };
			styles.setStyle(Styles::Progress, style);

			style = Style::defaultStyle();
			style.radius = 0;
			styles.setStyle(Styles::CloseButton, style);

			style.backgroundColor = Color{ 196,43,28 };
			styles.setStyle(Styles::CloseButtonHover, style);

			style.backgroundColor = Color{ 181,43,30 };
			styles.setStyle(Styles::CloseButtonPress, style);

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
		Label()
			: text_(nullptr)
		{
			setId(Styles::Label);
		}

		const char* text() const
		{
			return text_;
		}

		void setText(const char* text)
		{
			text_ = text;
		}

	protected:
		void draw(Painter& painter) override
		{
			if (text_)
			{
				auto style = Styles::instance().getStyle(id());
				painter.drawText(rect(), text_, style);
			}
		}

	private:
		const char* text_;
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

		using OnClickFunc = std::function<void()>;

		Button()
			: text_(nullptr)
			, state_(Normal)
		{
			setId(Styles::Button);
		}

		State state() const
		{
			return state_;
		}

		const char* text() const
		{
			return text_;
		}

		void setText(const char* text)
		{
			text_ = text;
		}

		void setOnClick(const OnClickFunc& fn)
		{
			onClick_ = fn;
		}

	protected:
		void draw(Painter& painter) override
		{
			auto style = Styles::instance().getStyle(id() + state_);
			painter.fillRoundRect(rect(), style.radius, style.backgroundColor);
			if (text_)
				painter.drawText(rect(), text_, style);
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
		const char* text_;
		State state_;
		OnClickFunc onClick_;
	};

	class Progress : public Widget
	{
	public:
		Progress()
			: step_(0)
		{
			setId(Styles::Progress);
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
		void draw(Painter& painter) override
		{
			auto style = Styles::instance().getStyle(id());
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
		Image()
			: bmp_(nullptr)
		{
			setId(Styles::Image);
		}

		void setBmpData(const void* data)
		{
			bmp_ = data;
		}

	protected:
		void draw(Painter& painter) override
		{
			if (bmp_)
				painter.drawImage(rect(), (const uint8_t*)bmp_);
		}

	private:
		const void* bmp_;
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

		close_ = new Button();
		close_->setId(Styles::CloseButton);
		close_->setRect(Rect{ titleRect_.width, 0,  48, 32 });
		close_->setOnDraw([=](Painter& painter)
			{
				auto& style = Styles::instance().getStyle(Styles::CloseButton);
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

#ifdef WIN32
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>

#include <string>
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

		inline int dpiScale(int origin, int dpi)
		{
			return MulDiv(origin, dpi, 96);
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

		Point scale(float num) const
		{
			return { int(float(x) * num), int(float(y) * num) };
		}
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

		Rect scale(float num) const
		{
			return
			{
				int(float(x) * num),
				int(float(y) * num),
				int(float(width) * num),
				int(float(height) * num)
			};
		}

		RECT toRect() const
		{
			return { x, y, x + width, y + height };
		}
	};

	struct Style
	{
		const char* name;
		Color color;
		Color backgroundColor;
		int radius;
		int fontSize;
		const char* fontFamily[6];

		static const Style& defaultStyle(bool isDark = false)
		{
			static Style styles[2] =
			{
				// light
				{
					"default-light",
					Color{50, 50, 50},
					Color{250, 250, 251},
					6,
					18,
					{"Microsoft YaHei UI", "SimSun", "sans-seirf", "sans", "Ariel", NULL}
				},
				// dark
				{
					"default-dark",
					Color{250, 250, 250},
					Color{ 34,34,38 },
					6,
					18,
					{"Microsoft YaHei UI", "SimSun", "sans-seirf", "sans", "Ariel", NULL}
				}
			};
			return styles[isDark];
		}
	};

	class Styles : public Handle
	{
	public:
		enum { MaxCount = 32 };

		static Styles& instance()
		{
			static Styles styles;
			return styles;
		}

		bool setStyle(const char* name, Style style)
		{
			style.name = name;
			int index = findStyle(name);

			if (index == -1)
				return addStyle(style);

			styles_[index] = style;
			return true;

		}

		const Style& getStyle(const char* name) const
		{
			int index = findStyle(name);
			if (index != -1)
				return styles_[index];

			return Style::defaultStyle();
		}

		void update();

	private:
		Styles()
		{

		}

		int findStyle(const char* name) const
		{
			for (int i = 0; i < MaxCount; ++i)
			{

				const Style& style = styles_[i];
				if (!style.name)
					return -1;

				if (strcmp(style.name, name) == 0)
					return i;
			}
			return -1;
		}

		bool addStyle(Style style)
		{
			if (index_ < MaxCount)
				styles_[index_++] = style;
			return index_ <= MaxCount;
		}

	private:
		Style styles_[MaxCount] = { 0 };
		int index_ = 0;
	};

	class Painter : public Handle
	{
	public:
		template <typename F> // F=void(Painter&)
		void withAA(const Rect& rect, const F& fn) const
		{
			Painter painter(mdc_, rect, scale_, 4);
			fn(painter);
		}

		void drawLine(int x, int y, int x1, int y1, int lineWidth, Color color)
		{
			HPEN pen = CreatePen(PS_SOLID, transform(lineWidth), color.toColorRef());
			HPEN old = (HPEN)SelectObject(mdc_, pen);

			Point p0 = transform(Point{ x, y });
			Point p1 = transform(Point{ x1, y1 });
			MoveToEx(mdc_, p0.x, p0.y, NULL);
			LineTo(mdc_, p1.x, p1.y);

			SelectObject(mdc_, old);
			DeleteObject(pen);
		}

		void drawText(const Rect& rect, const char* text, const Style& style)
		{
			HFONT oldFont;
			HFONT font = createFont(style);
			if (font)
				oldFont = (HFONT)SelectObject(mdc_, font);

			SetBkMode(mdc_, TRANSPARENT);
			auto str = utils::utf8ToUtf16(text);
			auto drawRect = rect.scale(scale_).toRect();
			auto oldColor = SetTextColor(mdc_, style.color.toColorRef());
			DrawText(mdc_, str.data, str.length, &drawRect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
			SetTextColor(mdc_, oldColor);

			if (font)
			{
				SelectObject(mdc_, oldFont);
				DeleteObject(font);
			}
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

			HDC dc = CreateCompatibleDC(mdc_);
			HBITMAP bitmap = CreateCompatibleBitmap(mdc_, width, height);
			HBITMAP old = (HBITMAP)SelectObject(dc, bitmap);
			BITMAPINFO bi = { 0 };
			bi.bmiHeader = *bih;
			SetDIBits(dc, bitmap, 0, height, pixels, &bi, DIB_RGB_COLORS);

			RECT rt = transform(rect).toRect();
			SetStretchBltMode(mdc_, HALFTONE);
			SetBrushOrgEx(mdc_, 0, 0, NULL);
			StretchBlt(mdc_, rt.left, rt.top, rt.right - rt.left, rt.bottom - rt.top, dc, 0, 0, width, height, SRCCOPY);

			SelectObject(dc, old);
			DeleteObject(bitmap);
			DeleteDC(dc);
		}

		void frameRect(const Rect& rect, int lineWidth, Color color)
		{
			RECT frame = transform(rect).toRect();
			HBRUSH brush = CreateSolidBrush(color.toColorRef());
			HRGN rgn = CreateRectRgn(frame.left, frame.top, frame.right, frame.bottom);
			FrameRgn(mdc_, rgn, brush, transform(lineWidth), transform(lineWidth));
			DeleteObject(brush);
			DeleteObject(rgn);
		}

		void fillRect(const Rect& rect, Color color)
		{
			RECT fill = transform(rect).toRect();
			HBRUSH brush = CreateSolidBrush(color.toColorRef());
			FillRect(mdc_, &fill, brush);
			DeleteObject(brush);
		}

		void fillRoundRect(const Rect& rect, int radius, Color color)
		{
			HBRUSH brush = CreateSolidBrush(color.toColorRef());
			RECT rt = transform(rect).toRect();
			HRGN hrgn = CreateRoundRectRgn(rt.left, rt.top, rt.right, rt.bottom, transform(radius), transform(radius));
			FillRgn(mdc_, hrgn, brush);
			DeleteObject(hrgn);
			DeleteObject(brush);
		}

		void roundRect(const Rect& rect, int lineWidth, int radius, Color color)
		{
			HBRUSH brush = CreateSolidBrush(color.toColorRef());
			RECT rt = transform(rect).toRect();
			HRGN hrgn = CreateRoundRectRgn(rt.left, rt.top, rt.right, rt.bottom, transform(radius), transform(radius));
			FrameRgn(mdc_, hrgn, brush, transform(lineWidth), transform(lineWidth));
			DeleteObject(hrgn);
			DeleteObject(brush);
		}

	private:
		friend class Window;

		Painter(HDC hdc, const Rect& rect, float scale, float ss)
			: hdc_(hdc)
			, rect_(rect)
			, sRect_(rect.scale(scale))
			, ssRect_(rect.scale(ss))
			, scale_(scale)
			, ss_(ss)
		{
			if (scale_ > ss_)
			{
				ss_ = scale_;
				ssRect_ = rect.scale(ss_);
			}
			ssRect_.x = 0;
			ssRect_.y = 0;

			mdc_ = CreateCompatibleDC(hdc);
			bitmap_ = CreateCompatibleBitmap(hdc, ssRect_.width, ssRect_.height);
			old_ = (HBITMAP)SelectObject(mdc_, bitmap_);
			SetStretchBltMode(mdc_, HALFTONE);
			SetBrushOrgEx(mdc_, 0, 0, NULL);
			StretchBlt(mdc_, ssRect_.x, ssRect_.y, ssRect_.width, ssRect_.height, hdc_, sRect_.x, sRect_.y, sRect_.width, sRect_.height, SRCCOPY);
		}

		~Painter()
		{
			SetStretchBltMode(hdc_, HALFTONE);
			SetBrushOrgEx(hdc_, 0, 0, NULL);
			StretchBlt(hdc_, sRect_.x, sRect_.y, sRect_.width, sRect_.height, mdc_, 0, 0, ssRect_.width, ssRect_.height, SRCCOPY);
			SelectObject(mdc_, old_);
			DeleteObject(bitmap_);
			DeleteDC(mdc_);
		}

		void setClipRect(const Rect& rect)
		{
			RECT rt = transform(rect).toRect();
			HRGN hrgn = CreateRectRgnIndirect(&rt);
			SelectClipRgn(mdc_, hrgn);
			DeleteObject(hrgn);
		}

		HFONT createFont(const Style& style)
		{
			if (style.fontFamily)
			{
				LOGFONT ft = { 0 };
				ft.lfHeight = style.fontSize * scale_;
				ft.lfQuality = CLEARTYPE_QUALITY; // clartype
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

		int transform(int num) const
		{
			return num * ss_;
		}

		Point transform(Point pt) const
		{
			return Point{ pt.x - rect_.x, pt.y - rect_.y }.scale(ss_);
		}

		Rect transform(Rect rect) const
		{
			return Rect{ rect.x - rect_.x, rect.y - rect_.y, rect.width, rect.height }.scale(ss_);
		}

	private:
		HDC hdc_;
		HDC mdc_;
		HBITMAP bitmap_;
		HBITMAP old_;
		Rect rect_;
		Rect sRect_; // scale rect
		Rect ssRect_; // super sample rect
		float scale_;
		float ss_; // super sample sacle
	};

	class Window;

	class Widget : public Handle
	{
	public:
		using OnDrawFunc = std::function<void(Painter&)>;

		virtual ~Widget() = default;

		const char* styleName() const
		{
			return name_;
		}

		void setStyleName(const char* name)
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
		Widget() = default;

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
		Rect rect_ = { 0 };
		const char* name_ = nullptr;
		Window* window_ = nullptr;
		OnDrawFunc onDraw_;
		bool visible_ = true;
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
			, rect_{ 0 }
			, close_(nullptr)
			, timerId_(0)
			, widgetIndex_(0)
			, dpi_(96)
			, scale_(1.0)
			, mouseWidget_(nullptr)
			, mouseIn_(false)
		{

		}

		~Window()
		{
			if (hwnd_)
				DestroyWindow(hwnd_);
		}

		bool create();

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
			rect_ = Rect{ 0, 0, width, height };
			SetWindowPos(hwnd_, NULL, 0, 0, utils::dpiScale(width, dpi_), utils::dpiScale(height, dpi_), SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED);
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
			case WM_ERASEBKGND:
				return 0;

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
				window->onMouseMove(Point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }, msg == WM_MOUSELEAVE);
				return 0;
			
			case WM_LBUTTONDOWN:
			case WM_LBUTTONUP:
				window->onMouseButton(msg == WM_LBUTTONDOWN);
				return 0;

			case 0x02E0: // WM_DPICHANGED
			{
				window->onDpiChanged(HIWORD(wParam));
				RECT* rect = (RECT*)lParam;
				SetWindowPos(hwnd, NULL, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
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
		
		void onDpiChanged(int dpi)
		{
			dpi_ = dpi;
			scale_ = float(dpi) / 96.0;
		}

		void onPaint(HDC hdc, int width, int height)
		{
			auto rect = Rect{ 0,0,width, height }.scale(1.0 / scale_);
			auto style = Styles::instance().getStyle("window");

			Painter painter(hdc, rect, scale_, scale_);
			painter.fillRect(rect, style.backgroundColor);

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
				if (widget->visible() && widget->rect().scale(scale_).contains(pt))
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
			return titleRect_.scale(scale_).contains(pt);
		}

		HWND hwnd_;
		const char* title_;
		Rect rect_;
		Rect titleRect_;
		Button* close_;
		int timerId_;
		int widgetIndex_;
		int dpi_;
		float scale_;
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
		static bool initialize(const char* appId)
		{
			initDpiAwareness();

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

			setStyles(isDarkMode());
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

		static void setStyles(bool darkMode)
		{
			Styles& styles = Styles::instance();
			auto style = Style::defaultStyle(darkMode);

			styles.setStyle("window", style);
			styles.setStyle("label", style);
			styles.setStyle("image", style);

			if (!darkMode)
			{
				// light
				style.backgroundColor = Color{ 230, 230, 230 };
				styles.setStyle("button", style);

				style.backgroundColor = Color{ 220,220,221 };
				styles.setStyle("button:hover", style);

				style.backgroundColor = Color{ 190,190,192 };
				styles.setStyle("button:press", style);

				style.color = Color{ 53,132,228 };
				style.backgroundColor = Color{ 235,232,230 };
				styles.setStyle("progress", style);
			}
			else
			{
				// dark
				style.backgroundColor = Color{ 56,56,59 };
				styles.setStyle("button", style);

				style.backgroundColor = Color{ 67,67,70 };
				styles.setStyle("button:hover", style);

				style.backgroundColor = Color{ 100,100,103 };
				styles.setStyle("button:press", style);

				style.color = Color{ 53,132,228 };
				style.backgroundColor = Color{ 81,81,85 };
				styles.setStyle("progress", style);
			}

			style = Style::defaultStyle(darkMode);
			style.radius = 0;
			styles.setStyle("CloseButton", style);

			style.backgroundColor = Color{ 196,43,28 };
			styles.setStyle("CloseButton:hover", style);

			style.backgroundColor = Color{ 181,43,30 };
			styles.setStyle("CloseButton:press", style);
		}

		static bool isDarkMode()
		{
			HKEY key;
			auto ret = RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &key);
			if (ret != ERROR_SUCCESS)
				return false;

			DWORD type;
			BYTE buf[8] = { 0 };
			DWORD len = 8;
			RegQueryValueEx(key, L"AppsUseLightTheme", NULL, &type, buf, &len);
			RegCloseKey(key);
			return buf[0] == 0;
		}

	private:
		static bool initDpiAwareness()
		{
			using SetProcessDpiAwarenessFunc = BOOL(*)(void*);
			auto setDpiAwareness = (SetProcessDpiAwarenessFunc)GetProcAddress(GetModuleHandle(TEXT("User32.dll")), "SetProcessDpiAwarenessContext");
			if (setDpiAwareness)
				return setDpiAwareness((void*)-4); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
			return false;
		}

		friend class Window;
		static int getDpiForWindow(HWND hwnd)
		{
			using GetDpiForWindowFunc = UINT(*)(void*);
			static auto getDpiForWindowFunc = (GetDpiForWindowFunc)GetProcAddress(GetModuleHandle(TEXT("User32.dll")), "GetDpiForWindow");
			static auto getDpiForWindow = getDpiForWindowFunc ? getDpiForWindowFunc : [](void*)->UINT { return 96; };
			return getDpiForWindow(hwnd);
		}
	};


	class Label : public Widget
	{
	public:
		Label()
			: text_(nullptr)
		{
			setStyleName("label");
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
				auto style = Styles::instance().getStyle(styleName());
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

		static const char* stateString(State state)
		{
			const char* strings[] = { "", ":hover", ":press" };
			return strings[state];
		}

		using OnClickFunc = std::function<void()>;

		Button()
			: text_(nullptr)
			, state_(Normal)
		{
			setStyleName("button");
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
			auto name = std::string(styleName()) + stateString(state_);
			auto style = Styles::instance().getStyle(name.c_str());
			painter.withAA(rect(), [=](Painter& aaPainter)
				{
					aaPainter.fillRoundRect(rect(), style.radius, style.backgroundColor);
				}
			);

			if (text_)
				painter.drawText(rect(), text_, style); // text not need AA
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
			setStyleName("progress");
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
			auto style = Styles::instance().getStyle(styleName());
			painter.withAA(rect(), [=](Painter& aaPainter)
				{
					aaPainter.fillRoundRect(rect(), style.radius, style.backgroundColor);
					if (0 < step_)
					{
						Rect stepRect = rect();
						stepRect.width *= step_;
						aaPainter.fillRoundRect(stepRect, style.radius, style.color);
					}
				}
			);
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
			setStyleName("image");
		}

		void setBmpData(const void* data)
		{
			bmp_ = data;
		}

	protected:
		void draw(Painter& painter) override
		{
			if (bmp_)
			{
				painter.withAA(rect(), [=](Painter& aaPainter)
					{
						aaPainter.drawImage(rect(), (const uint8_t*)bmp_);
					}
				);
			}
		}

	private:
		const void* bmp_;
	};

	inline bool Window::create()
	{
		int style = WS_OVERLAPPED | WS_CAPTION | WS_THICKFRAME;
		HWND hwnd = CreateWindowEx(0, WndClass, L"Window", style, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, NULL, NULL);
		if (!hwnd || hwnd == INVALID_HANDLE_VALUE)
			return false;

		onDpiChanged(Application::getDpiForWindow(hwnd));

		MARGINS margin = { 1,1,1,1 };
		::DwmExtendFrameIntoClientArea(hwnd, &margin);

		hwnd_ = hwnd;
		SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)this);
		return true;
	}

	inline void Window::show()
	{
		UpdateWindow(hwnd_);
		ShowWindow(hwnd_, SW_NORMAL);

		titleRect_ = Rect{ 0, 0, rect_.width - 48, 32 };

		close_ = new Button();
		close_->setStyleName("CloseButton");
		close_->setRect(Rect{ titleRect_.width, 0,  48, 32 });
		close_->setOnDraw([=](Painter& painter)
			{
				Rect rect = close_->rect();
				painter.withAA(rect, [=](Painter& aaPainter)
					{
						auto name = std::string("CloseButton") + Button::stateString(close_->state());
						auto& style = Styles::instance().getStyle(name.c_str());
						// draw 12 x 12  x
						int xCenter = rect.x + rect.width / 2;
						int yCenter = rect.y + rect.height / 2;
						int xWidth = 12;
						int xHeight = 12;
						aaPainter.drawLine(xCenter, yCenter, xCenter - 6, yCenter + 6, 1, style.color);
						aaPainter.drawLine(xCenter, yCenter, xCenter + 6, yCenter + 6, 1, style.color);
						aaPainter.drawLine(xCenter, yCenter, xCenter + 6, yCenter - 6, 1, style.color);
						aaPainter.drawLine(xCenter, yCenter, xCenter - 6, yCenter - 6, 1, style.color);
					}
				);
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

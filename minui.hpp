#pragma once

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

		void update() {}

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

		void drawImage(const Rect& rect, const uint8_t* bmp, int size)
		{
			if (bmp[0] != 0x42 && bmp[1] != 0x4d)
				return;

			BITMAPFILEHEADER* bfh = (BITMAPFILEHEADER*)bmp;
			if (bfh->bfSize > size)
				return;

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
			ATOM atom = RegisterClassEx(&wcx);
			if (atom == 0)
				return false;

			setStyles(isDarkMode());
			return true;
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

		void setBmpData(const void* data, int size)
		{
			bmp_ = data;
			size_ = size;
		}

	protected:
		void draw(Painter& painter) override
		{
			if (bmp_)
			{
				painter.withAA(rect(), [=](Painter& aaPainter)
					{
						aaPainter.drawImage(rect(), (const uint8_t*)bmp_, size_);
					}
				);
			}
		}

	private:
		const void* bmp_;
		int size_;
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

#ifdef __linux__

#include <dlfcn.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>

#include <string>
#include <thread>
#include <mutex>
#include <sstream>
#include <functional>
#include <condition_variable>

namespace minui
{
	namespace gtk
	{
		using Error = void;
		using Widget = void;
		using Window = void;
		using Fixed = void;
		using Label = void;
		using Button = void;
		using ProgressBar = void;
		using Image = void;
		using CssProvider = void;
		using Application = void;
		using Callback = void(*)();
		using SourceFunc = bool(*)(void*);

		enum
		{
			CONNECT_DEFAULT = 0,

			SOURCE_REMOVE = false,
			SOURCE_CONTINUE = true,

			APPLICATION_DEFAULT_FLAGS = 0,

			PACK__START = 0,
			PACK_END = 1,

			STYLE_PROVIDER_PRIORITY_APPLICATION = 600,
		};

		class Library
		{
		public:
			static Library& instance()
			{
				static Library lib;
				return lib;
			}

			bool initialize()
			{
				#define DUMMY(p) p = (decltype(p))dummy
				DUMMY(adw_init);
				DUMMY(adw_style_manager_get_default);
				DUMMY(adw_style_manager_get_dark);
				#undef DUMMY

				gtk_ = dlopen("libgtk-4.so", RTLD_LAZY);
				if (gtk_)
				{
					adw_ = dlopen("libadwaita-1.so", RTLD_LAZY);
					if (adw_)
					{
						#define SYMBOL(p) p = (decltype(p))dlsym(adw_, #p); if (!p) return false
						SYMBOL(adw_init);
						SYMBOL(adw_style_manager_get_default);
						SYMBOL(adw_style_manager_get_dark);
						#undef SYMBOL
					}
				}

				if (!gtk_)
				{
					gtk_ = dlopen("libgtk-3.so.0", RTLD_LAZY);
					if (!gtk_)
						return false;

					isGtk3_ = true;
				}

				#define SYMBOL(p) p = (decltype(p))dlsym(gtk_, #p); if (!p) return false
				#define SYMBOL_WITH(p, sym) p = (decltype(p))dlsym(gtk_, sym); if (!p) return false
				#define SYMBOL_SELECT(p, sym4, sym3) p = (decltype(p))dlsym(gtk_, isGtk3_ ? sym3 : sym4); if (!p) return false
				SYMBOL(g_signal_connect_data);
				SYMBOL(g_application_hold);
				SYMBOL(g_application_run);
				SYMBOL(g_application_quit);
				SYMBOL(g_idle_add);
				SYMBOL(g_timeout_add);
				SYMBOL(g_memory_input_stream_new_from_data);
				SYMBOL(g_input_stream_close);

				SYMBOL(gtk_init);
				SYMBOL(gtk_application_new);

				SYMBOL_WITH(gtk_window_new_, "gtk_window_new");
				SYMBOL(gtk_window_set_decorated);
				SYMBOL(gtk_window_set_resizable);
				SYMBOL_SELECT(gtk_window_set_child, "gtk_window_set_child", "gtk_container_add");
				SYMBOL(gtk_window_set_title);
				SYMBOL(gtk_window_set_titlebar);
				SYMBOL(gtk_window_set_default_size);
				SYMBOL_SELECT(gtk_window_present, "gtk_window_present", "gtk_widget_show_all");
				SYMBOL(gtk_window_close);

				SYMBOL(gtk_header_bar_new);
				SYMBOL(gtk_header_bar_set_decoration_layout);
				SYMBOL_SELECT(gtk_header_bar_set_title_widget, "gtk_header_bar_set_title_widget", "gtk_header_bar_set_custom_title");
				SYMBOL_SELECT(gtk_header_bar_set_show_title_buttons, "gtk_header_bar_set_show_title_buttons", "gtk_header_bar_set_show_close_button");

				SYMBOL(gtk_widget_queue_draw);
				SYMBOL(gtk_widget_set_visible);
				SYMBOL(gtk_widget_set_size_request);

				SYMBOL(gtk_fixed_new);
				SYMBOL_WITH(gtk_fixed_put_, "gtk_fixed_put");

				SYMBOL(gtk_label_new);
				SYMBOL(gtk_label_set_text);

				SYMBOL(gtk_button_new);
				SYMBOL(gtk_button_set_label);

				SYMBOL(gtk_progress_bar_new);
				SYMBOL(gtk_progress_bar_set_fraction);

				SYMBOL(gtk_image_new);
				SYMBOL(gtk_image_set_from_pixbuf);

				SYMBOL(gtk_css_provider_new);
				SYMBOL_WITH(gtk_css_provider_load_from_data_, "gtk_css_provider_load_from_data");

				SYMBOL(gdk_pixbuf_new_from_stream);
				SYMBOL(gdk_pixbuf_new_from_stream_at_scale);
				SYMBOL(gdk_display_get_default);

				if (isGtk3_)
				{
					SYMBOL(gtk_widget_get_style_context);
					SYMBOL(gtk_style_context_add_class);
					SYMBOL(gtk_style_context_add_provider_for_screen);
					SYMBOL(gdk_display_get_default_screen);
				}
				else
				{
					SYMBOL_WITH(gtk_widget_add_css_class_, "gtk_widget_add_css_class");
					SYMBOL_WITH(gtk_style_context_add_provider_for_display_, "gtk_style_context_add_provider_for_display");
				}

				#undef SYMBOL
				#undef SYMBOL_WITH
				#undef SYMBOL_SELECT

				return true;
			}

		public:
			#define FUNC(RET, NAME, PARAMS) RET(*NAME)PARAMS = nullptr
			// glib
			FUNC(int,  g_signal_connect_data, (void* obj, const char* sig, Callback callback, void* data, void* destroy, int flags));
			FUNC(void, g_application_hold,    (void* app));
			FUNC(int,  g_application_run,     (void* app, int argc, const char** argv));
			FUNC(void, g_application_quit,    (void* app));

			FUNC(int,  g_idle_add,    (SourceFunc fn, void* data));
			FUNC(int,  g_timeout_add, (int interval, SourceFunc fn, void* data));

			FUNC(void*, g_memory_input_stream_new_from_data, (const void* data,  int len, void* destroy));
			FUNC(void,  g_input_stream_close,                (void* s, void* cancel, void* err));
			
			// gtk
			FUNC(void,  gtk_init,            ());
			FUNC(void*, gtk_application_new, (const char* appid, int flag));

			void* gtk_window_new()
			{
				using gtk3_window_new = void*(*)(int);
				using gtk4_window_new = void*(*)();
				if (isGtk3_)
					return (gtk3_window_new(gtk_window_new_))(0);
				else
					return (gtk4_window_new(gtk_window_new_))();
			}

			FUNC(void,  gtk_window_set_decorated, (void* win, bool v));
			FUNC(void,  gtk_window_set_resizable, (void* win, bool v));
			FUNC(void,  gtk_window_set_child,     (void* win, void* child));
			FUNC(void,  gtk_window_set_title,     (void* win, const char* text));
			FUNC(void,  gtk_window_set_titlebar,  (void* win, void* widget));
			FUNC(void,  gtk_window_set_default_size, (void* win, int width, int height));
			FUNC(void,  gtk_window_present,          (void* win));
			FUNC(void,  gtk_window_close,          (void* win));

			FUNC(void*, gtk_header_bar_new, ());
			FUNC(void,  gtk_header_bar_set_title_widget, (void* hb, void* widget));
			FUNC(void,  gtk_header_bar_set_decoration_layout, (void* hb, const char* layout));
			FUNC(void,  gtk_header_bar_set_show_title_buttons, (void* hb, bool v));

			FUNC(void,  gtk_widget_queue_draw, (void* w));

			void gtk_widget_add_css_class(void* w, const char* cls)
			{
				if (isGtk3_)
					gtk_style_context_add_class(gtk_widget_get_style_context(w), cls);
				else
					gtk_widget_add_css_class_(w, cls);
			}

			FUNC(void,  gtk_widget_set_visible, (void* w, bool v));
			FUNC(void,  gtk_widget_set_size_request, (void* w, int width, int height));

			FUNC(void*, gtk_fixed_new, ());

			void gtk_fixed_put(void* fixed, void* child, int x, int y)
			{
				using gtk3_fixed_put = void(*)(void*, void*, int x, int y);
				using gtk4_fixed_put = void(*)(void*, void*, double x, double y);

				if (isGtk3_)
					(gtk3_fixed_put(gtk_fixed_put_))(fixed, child, x, y);
				else
					(gtk4_fixed_put(gtk_fixed_put_))(fixed, child, x, y);
			}

			FUNC(void*, gtk_label_new, (const char* text));
			FUNC(void,  gtk_label_set_text, (void* label, const char* text));

			FUNC(void*, gtk_button_new, ());
			FUNC(void,  gtk_button_set_label, (void* btn, const char* text));

			FUNC(void*, gtk_progress_bar_new, ());
			FUNC(void,  gtk_progress_bar_set_fraction, (void* pgs, double step));

			FUNC(void*, gtk_image_new, ());
			FUNC(void,  gtk_image_set_from_pixbuf, (void* img, void* pixbuf));

			FUNC(void*, gtk_css_provider_new, ());

			void gtk_css_provider_load_from_data(void* prov, const char* css, intptr_t len)
			{
				using gtk3_css_provider_load_from_data = void(*)(void* prov, const char* css, intptr_t len, void* err);
				using gtk4_css_provider_load_from_data = void(*)(void* prov, const char* css, intptr_t len);
				if (isGtk3_)
				{
					void* err = nullptr;
					(gtk3_css_provider_load_from_data(gtk_css_provider_load_from_data_))(prov, css, len, &err);
				}
				else
				{
					(gtk4_css_provider_load_from_data(gtk_css_provider_load_from_data_))(prov, css, len);
				}
			}

			void gtk_style_context_add_provider_for_display(void* dis, void* prov, int prvi)
			{
				if (isGtk3_)
					gtk_style_context_add_provider_for_screen(gdk_display_get_default_screen(dis), prov, prvi);
				else
					gtk_style_context_add_provider_for_display_(dis, prov, prvi);
			}

			// gdk
			FUNC(void*, gdk_pixbuf_new_from_stream, (void* s, void* cancel, void* err));
			FUNC(void*, gdk_pixbuf_new_from_stream_at_scale, (void* s, int width, int height, bool aspect_radio, void* cancel, void* err));
			FUNC(void*, gdk_display_get_default, ());

			// adwaita
			FUNC(void,  adw_init,                     ());
			FUNC(void*, adw_style_manager_get_default,());
			FUNC(bool,  adw_style_manager_get_dark,   (void* mgr));

			void set_window_titlebar(void* win, void* titlebar)
			{
				if (isGtk3_)
					gtk_window_set_titlebar(win, titlebar);
			}

			using OnCloseFunc = bool(*)(void* self, void* data);
			void connect_window_close_request(void* win, OnCloseFunc onClose, void* data)
			{
				if (isGtk3_)
				{
					using DeleteEventFunc = bool(*)(void* w, void* e, void* user);
					struct DeleteEventArg
					{
						OnCloseFunc onClose_;
						void* data_;
					};
					auto arg = new DeleteEventArg{onClose, data};

					g_signal_connect_data(win, "delete_event", Callback(DeleteEventFunc([](void* w, void* e, void* user)->bool
					{
						auto arg = (DeleteEventArg*)user;
						auto ret = arg->onClose_(w, arg->data_);
						delete arg;
						return ret;
					})), arg, nullptr, CONNECT_DEFAULT);
				}
				else
				{
					g_signal_connect_data(win, "close-request", gtk::Callback(onClose), data, nullptr, gtk::CONNECT_DEFAULT);
				}
			}

		private:
			// gtk3
			FUNC(void*, gtk_widget_get_style_context, (void* w));
			FUNC(void*, gtk_style_context_add_class,  (void* sc, const char* cls));
			FUNC(void*, gdk_display_get_default_screen, (void* display));
			FUNC(void,  gtk_style_context_add_provider_for_screen, (void* screen, void* prov, int prvi));

			// gtk4
			FUNC(void*, gtk_widget_add_css_class_,    (void* w, const char* cls));
			FUNC(void, gtk_style_context_add_provider_for_display_, (void* display, void* prov, int prvi));

			// diff
			void* gtk_window_new_ = nullptr;
			void* gtk_fixed_put_ = nullptr;
			void* gtk_css_provider_load_from_data_ = nullptr;
			#undef FUNC
		private:
			Library() = default;

			static void* dummy() { return nullptr; }

			void* gtk_ = nullptr;
			void* adw_ = nullptr;

			bool isGtk3_ = false;
		};

		inline Library& lib() { return Library::instance(); }
	}

	namespace utils
	{
		struct ConditionContext
		{
			std::mutex mtx_;
			std::condition_variable cond_;

			void notify()
			{
				cond_.notify_one();
			}

			void wait()
			{
				std::unique_lock<std::mutex> lock(mtx_);
				cond_.wait(lock);
			}
		};
	}


	struct Color
	{
		uint8_t r;
		uint8_t g;
		uint8_t b;
		uint8_t _;
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
	};

	struct Style
	{
		enum { FontFamilyCount = 6 };

		const char* name;
		Color color;
		Color backgroundColor;
		int radius;
		int fontSize;
		const char* fontFamily[FontFamilyCount];

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
					14,
					{"sans", NULL}
				},
				// dark
				{
					"default-dark",
					Color{250, 250, 250},
					Color{ 34,34,38 },
					6,
					14,
					{"sans", NULL}
				}
			};
			return styles[isDark];
		}
	};

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

		void update()
		{
			updateCss();
		}

	private:
		friend class Application;		
		void initialize()
		{
			initCss();
		}

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

		void initCss();
		void updateCss();

		gtk::CssProvider* css_ = nullptr;
		Style styles_[MaxCount];
		int index_ = 0;
	};

	class Window;

	class Widget : public Handle
	{
	public:
		virtual ~Widget() = default;

		const char* styleName() const
		{
			return name_;
		}

		void setStyleName(const char* name);

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

		void setVisible(bool v);

	protected:
		Widget() = default;

		void setHandle(gtk::Widget* handle)
		{
			handle_ = handle;
		}

	private:
		friend class Window;
		void setWindow(Window* window)
		{
			window_ = window;
		}

		gtk::Widget* handle() const
		{
			return handle_;
		}

	private:
		gtk::Widget* handle_ = nullptr;
		Window* window_ = nullptr;
		Rect rect_ = {0};
		const char* name_ = nullptr;
		bool visible_ = true;
	};
	
	class Application : public Handle
	{
	public:
		static bool initialize(const char* appId)
		{
			bool ok = gtk::lib().initialize();
			if (!ok)
				return false;

			gtk::lib().gtk_init();
			gtk::lib().adw_init();

			instance().app_ = gtk::lib().gtk_application_new(appId, gtk::APPLICATION_DEFAULT_FLAGS);
			gtk::lib().g_signal_connect_data(instance().app_, "activate", (gtk::Callback)([](){}), nullptr, nullptr, gtk::CONNECT_DEFAULT);

			instance().ui_ = std::thread([]()
			{
				auto app = instance().app_;
				gtk::lib().g_application_hold(app); // never stop
				gtk::lib().g_application_run(app, 0, nullptr);
			});

			Styles::instance().initialize();
			setStyles(isDarkMode());
			return true;
		}

		static void exec()
		{
			instance().ui_.join();
		}

		static void quit()
		{
			gtk::lib().g_application_quit(instance().app_);
		}

		static void setStyles(bool darkMode)
		{
			Styles& styles = Styles::instance();
			auto style = Style::defaultStyle(darkMode);
			styles.setStyle("window", style);
			styles.setStyle("label", style);
			styles.setStyle("image", style);
			styles.setStyle("headerbar", style);

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
			styles.update();
		}

		static bool isDarkMode()
		{
			auto styleMgr = gtk::lib().adw_style_manager_get_default();
			return gtk::lib().adw_style_manager_get_dark(styleMgr);
		}

		using RunFunc = std::function<void()>;

		static void runOnUI(const RunFunc& fn)
		{
			if (std::this_thread::get_id() == instance().ui_.get_id())
			{
				fn();
				return;
			}

			struct RunContext : utils::ConditionContext
			{
				RunFunc run_;
			};

			RunContext ctx;
			ctx.run_ = fn;

			gtk::lib().g_idle_add((gtk::SourceFunc)([](void* data) -> bool
			{
				auto ctx = (RunContext*)data;
				ctx->run_();
				ctx->notify();
				return gtk::SOURCE_REMOVE;
			}), &ctx);

			ctx.wait();
			return;
		}

	private:
		friend class Window;
		static Application& instance()
		{
			static Application app;
			return app;
		}

	private:
		gtk::Application* app_;
		std::thread ui_;
	};

	class Window : public Handle
	{
	public:
		using TimerFunc = std::function<bool()>;
		using OnCloseFunc = std::function<void()>;

		enum
		{
			TimerCount = 32,
			WidgetCount = 64
		};

		Window() = default;
		~Window() = default;

		bool create()
		{
			Application::runOnUI([=]()
			{
				handle_ = gtk::lib().gtk_window_new();
				if (handle_)
				{
					gtk::lib().gtk_window_set_decorated(handle_, false);
					gtk::lib().gtk_window_set_resizable(handle_, false);
					gtk::lib().connect_window_close_request(handle_, onClose, this);

					fixed_ = gtk::lib().gtk_fixed_new();
					gtk::lib().gtk_window_set_child(handle_, fixed_);
				}
			});
			return handle_ != nullptr;
		}

		const char* title() const
		{
			return title_;
		}

		void setTitle(const char* text)
		{
			title_ = text;
			Application::runOnUI([=]()
			{
				gtk::lib().gtk_window_set_title(handle_, title_);
			});
		}

		void setSize(int width, int height)
		{
			rect_ = Rect{ 0, 0, width, height };
			Application::runOnUI([=]()
			{
				gtk::lib().gtk_window_set_default_size(handle_, width, height);
			});
		}

		bool addWidget(Widget* w)
		{
			Application::runOnUI([=]()
			{
				if (widgetIndex_ < WidgetCount)
				{
					widgets_[widgetIndex_++] = w;
					w->setWindow(this);
					auto handle = w->handle();
					Rect rect = w->rect();
					gtk::lib().gtk_widget_set_size_request(handle, rect.width, rect.height);
					gtk::lib().gtk_fixed_put(fixed_, handle, rect.x, rect.y);
				}
			});
			return widgetIndex_ >= WidgetCount;
		}

		bool addTimer(int msec, const TimerFunc& fn)
		{
			Application::runOnUI([=]()
			{
				if (timerIndex_ < TimerCount)
				{
					timers_[timerIndex_] = fn;
					gtk::lib().g_timeout_add(msec, gtk::SourceFunc(onTimeout), &timers_[timerIndex_]);
					timerIndex_++;
				}
			});
			return timerIndex_ >= TimerCount;
		}

		void show()
		{
			Application::runOnUI([=]()
			{
				titleBar_ = gtk::lib().gtk_header_bar_new();
				gtk::lib().gtk_widget_set_size_request(titleBar_, rect_.width + 10, 30);
				gtk::lib().gtk_header_bar_set_decoration_layout(titleBar_, "menu:close");
				gtk::lib().gtk_header_bar_set_title_widget(titleBar_, gtk::lib().gtk_label_new(""));
				gtk::lib().gtk_header_bar_set_show_title_buttons(titleBar_, true);

				gtk::lib().gtk_fixed_put(fixed_, titleBar_, 0, 0);
				gtk::lib().set_window_titlebar(handle_, titleBar_);

				gtk::lib().gtk_window_present(handle_);

				for (int i = 0; i < widgetIndex_; ++i)
				{
					auto widget = widgets_[i];
					widget->setVisible(widget->visible()); // keep initial visible
				}
			});
		}

		void update()
		{
			Application::runOnUI([=]()
			{
				gtk::lib().gtk_widget_queue_draw(handle_);
			});
		}

		void close()
		{
			Application::runOnUI([=]()
			{
				gtk::lib().gtk_window_close(handle_);
			});
		}

		void setOnClose(const OnCloseFunc fn)
		{
			Application::runOnUI([=]()
			{
				onClose_ = fn;
			});
		}

		void setCloseable(bool v)
		{
			closeable_ = v;
			Application::runOnUI([=]()
			{
				gtk::lib().gtk_header_bar_set_show_title_buttons(titleBar_, v);
			});
		}

	private:
		static bool onClose(void* obj, void* data)
		{
			auto self = (Window*)data;
			if (self->closeable_ && self->onClose_)
				self->onClose_();
			return self->closeable_;
		}

		static bool onTimeout(void* data)
		{
			auto pf = (TimerFunc*)data;
			bool stop = (*pf)();
			return stop ? gtk::SOURCE_REMOVE : gtk::SOURCE_CONTINUE;
		}

	private:
		gtk::Window* handle_ = nullptr;
		gtk::Fixed* fixed_ = nullptr;
		gtk::Widget* titleBar_ = nullptr;
		const char* title_ = nullptr;
		Rect rect_ = {0, 0, 0, 0};
		int timerIndex_ = 0;
		int widgetIndex_ = 0;
		TimerFunc timers_[TimerCount];
		Widget* widgets_[WidgetCount];
		OnCloseFunc onClose_;
		bool closeable_ = true;
	};
	
	class Label : public Widget
	{
	public:
		Label()
		{
			Application::runOnUI([=]()
			{
				handle_ = gtk::lib().gtk_label_new("");
				setHandle(handle_);
				setStyleName("Label");
			});
		}

		const char* text() const
		{
			return text_;
		}

		void setText(const char* text)
		{
			text_ = text;
			Application::runOnUI([=]()
			{
				gtk::lib().gtk_label_set_text(handle_, text_);
			});
		}

	private:
		gtk::Label* handle_ = nullptr;
		const char* text_ = nullptr;
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
		{
			Application::runOnUI([=]()
			{
				handle_ = gtk::lib().gtk_button_new();
				setHandle(handle_);
				setStyleName("Button");
			});
		}

		const char* text() const
		{
			return text_;
		}

		void setText(const char* text)
		{
			text_ = text;
			Application::runOnUI([=]()
			{
				gtk::lib().gtk_button_set_label(handle_, text_);
			});
		}

		void setOnClick(const OnClickFunc& fn)
		{
			onClick_ = fn;
			Application::runOnUI([=]()
			{
				gtk::lib().g_signal_connect_data(handle_, "clicked", gtk::Callback(onClick), this, nullptr, gtk::CONNECT_DEFAULT);
			});
		}

	private:
		static void onClick(void* obj, void* data)
		{
			auto self = (Button*)data;
			self->onClick_();
		}

	private:
		gtk::Button* handle_ = nullptr;
		const char* text_ = nullptr;
		OnClickFunc onClick_;
	};

	class Progress : public Widget
	{
	public:
		Progress()
		{
			Application::runOnUI([=]()
			{
				handle_ = gtk::lib().gtk_progress_bar_new();
				setHandle(handle_);
				setStyleName("Progress");
			});
		}

		void setStep(float step)
		{
			if (0 <= step && step <= 1.0)
			{
				Application::runOnUI([=]()
				{
					gtk::lib().gtk_progress_bar_set_fraction(handle_, step);
				});
			}
		}

	private:
		gtk::ProgressBar* handle_ = nullptr;
	};

	class Image : public Widget
	{
	public:
		Image()
		{
			Application::runOnUI([=]()
			{
				handle_ = gtk::lib().gtk_image_new();
				setHandle(handle_);
				setStyleName("Image");
			});
		}

		void setBmpData(const void* data, int size)
		{
			bmp_ = data;
			Application::runOnUI([=]()
			{
    			auto stream = gtk::lib().g_memory_input_stream_new_from_data(bmp_, size, NULL);
    
				gtk::Error* error = nullptr;
				auto pixbuf = gtk::lib().gdk_pixbuf_new_from_stream_at_scale(stream, rect().width, rect().height, false, nullptr, &error);
				gtk::lib().g_input_stream_close(stream, nullptr, nullptr);
				if (error)
					return;

				gtk::lib().gtk_image_set_from_pixbuf(handle_, pixbuf);
			});
		}

	private:
		gtk::Image* handle_ = nullptr;
		const void* bmp_ = nullptr;
	};

	inline void Styles::initCss()
	{
		Application::runOnUI([=]()
		{
			css_ = gtk::lib().gtk_css_provider_new();
			gtk::lib().g_signal_connect_data(css_, "parsing-error", gtk::Callback([](){}), nullptr, nullptr, gtk::CONNECT_DEFAULT);
			gtk::lib().gtk_css_provider_load_from_data(css_, "window {}", -1);
    		gtk::lib().gtk_style_context_add_provider_for_display(gtk::lib().gdk_display_get_default(), css_, gtk::STYLE_PROVIDER_PRIORITY_APPLICATION);
		});
	}

	inline void Styles::updateCss()
	{
		auto nameToSel = [](const char* name) -> std::string
		{
			std::string str;
			// fixed names
			if (strcmp(name, "window") == 0) str = "window";
			else if (strcmp(name, "headerbar") == 0) str = "headerbar";
			else if (strcmp(name, "label") == 0) str = ".Label";
			else if (strcmp(name, "button") == 0) str = ".Button";
			else if (strcmp(name, "button:hover") == 0) str = ".Button:hover";
			else if (strcmp(name, "button:press") == 0) str = ".Button:press";
			else str = "." + std::string(name);

			// prefix
			auto pos = str.rfind(":press");
			if (pos == std::string::npos)
				return str;

			std::string res = str.substr(0, pos);
			res.append(":active");
			return res;			
		};

		std::ostringstream buf;
		buf << "headerbar { border: 0px; outline: none; background-image: none; box-shadow: none; text-shadow: none; }\n";
		buf << "headerbar button { color: rgb(110, 110, 110); outline: none; box-shadow: none; -gtk-icon-shadow: none; }\n";
		for (int i = 0; i < MaxCount; ++i)
		{
			const Style& style = styles_[i];
			if (!style.name)
				break;

			buf << nameToSel(style.name) << "{\n"
			<< "\t" << "border: 0px; outline: none; background-image: none; box-shadow: none; text-shadow: none; \n"
			<< "\t" << "color: rgb(" <<  (int)style.color.r << "," << (int)style.color.g << "," << (int)style.color.b << ");\n"
			<< "\t" << "background-color: rgb(" <<  (int)style.backgroundColor.r << "," << (int)style.backgroundColor.g << "," << (int)style.backgroundColor.b << ");\n"
			<< "\t" << "border-radius: " << style.radius << "px;\n"
			<< "\t" << "font-size: " << style.fontSize << "px;\n"
			<< "\t" << "font-family: \"";
			for (int j = 0; j < Style::FontFamilyCount; j++)
			{
				const char* fontName = style.fontFamily[j];
				if (!fontName)
					break;
				buf << fontName << ",";
			}
			buf << "\";\n"
			<< "}\n";
		}

		std::string str = buf.str();

		Application::runOnUI([=]()
		{
			gtk::lib().gtk_css_provider_load_from_data(css_, str.c_str(), str.length());
		});
	}

	inline void Widget::setStyleName(const char* name)
	{
		name_ = name;
		Application::runOnUI([=]()
		{
			gtk::lib().gtk_widget_add_css_class(handle_, name);
		});
	}

	inline void Widget::setVisible(bool v)
	{
		visible_ = v;
		Application::runOnUI([=]()
		{
			gtk::lib().gtk_widget_set_visible(handle_, v);
		});
	}
}

#endif
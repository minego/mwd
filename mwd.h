#ifndef _MWD_H
#define _MWD_H

#define _POSIX_C_SOURCE 200809L
#include <getopt.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>
#include <linux/input-event-codes.h>

#include <wlr/xwayland.h>

#ifndef TRUE
# define TRUE 1
#endif

#ifndef FALSE
# define FALSE 0
#endif

typedef enum mwdGrabMode {
	MWD_GRAB_NONE,
	MWD_GRAB_MOVE,
	MWD_GRAB_RESIZE
} mwdGrabMode;

typedef enum mwdLayer {
	MWD_LAYER_BEFORE,

	MWD_LAYER_BACKGROUND,
	MWD_LAYER_BOTTOM,
	MWD_LAYER_NORMAL,
	MWD_LAYER_TOP,
	MWD_LAYER_OVERLAY,

	MWD_LAYER_AFTER
} mwdLayer;

typedef struct mwdServer
{
	struct wl_display					*display;
	struct wlr_backend					*backend;
	struct wlr_renderer					*renderer;
	struct wlr_compositor				*compositor;
	struct wlr_seat						*seat;
	struct wlr_output_layout			*layout;
	struct wl_listener					layoutChanged;

	struct wlr_cursor					*cursor;
	struct wlr_xcursor_manager			*cursorMgr;

	struct {
		struct wl_list					drawOrder;
		struct wl_list					userOrder;
	} views;
	struct wl_list						keyboards;
	struct wl_list						outputs;

	struct {
		struct wlr_output_manager_v1	*mgr;
		struct wl_listener				added;
		struct wl_listener				apply;
		struct wl_listener				test;

		struct mwdOutputTest			*pendingTest;
		bool							applying;
	} output;

	struct wl_listener					cursorMotionRelative;
	struct wl_listener					cursorMotionAbsolute;
	struct wl_listener					cursorButton;
	struct wl_listener					cursorAxis;
	struct wl_listener					cursorFrame;

	struct wl_listener					newInput;
	struct wl_listener					requestCursor;
	struct wl_listener					setSelection;
	uint32_t							modifiers;

	struct {
		struct mwdView					*view;

		struct {
			double						x, y;
		} cursor;

		uint32_t						edges;
		double							top, right, bottom, left;

		mwdGrabMode						mode;
	} grab;

	struct {
		struct wlr_layer_shell_v1		*shell;
		struct wl_listener				newSurface;
	} layerShell;

	struct {
		struct wlr_xdg_shell			*shell;
		struct wl_listener				newSurface;
	} xdgShell;

	struct {
		struct wlr_xwayland				*shell;
		struct wl_listener				ready;
		struct wl_listener				newSurface;
	} xwayland;
} mwdServer;

typedef struct mwdOutput
{
	struct wl_list						link;
	mwdServer							*server;

	struct wlr_output					*output;
	struct wl_listener					frame;
	bool								enabled;
} mwdOutput;

typedef struct mwdOutputTest
{
	mwdServer							*server;
	struct wl_event_source				*timer;

	struct wlr_output_configuration_v1	*oldConfig;
	struct wlr_output_configuration_v1	*newConfig;
} mwdOutputTest;

typedef struct mwdView
{
	struct {
		struct wl_list					drawOrder;
		struct wl_list					userOrder;
	} link;

	mwdServer							*server;
	enum {
		MWD_UNKNOWN,
		MWD_XDG_SHELL,
		MWD_LAYER_SHELL,
		MWD_XWAYLAND_SHELL
	} type;
	struct mwdViewInterface				*cb;

	union {
		struct {
			struct wlr_xdg_surface		*surface;
			bool						activated;
		} xdg;

		struct {
			struct wlr_layer_surface_v1	*surface;
		} layer;

		struct {
			struct wlr_xwayland_surface	*surface;
			bool						activated;
		} xwayland;
	};

	struct wl_listener					map;
	struct wl_listener					unmap;
	struct wl_listener					destroy;
	struct wl_listener					requestMove;
	struct wl_listener					requestResize;
	bool								mapped;

	uint32_t							edges;
	double								top, right, bottom, left;
	mwdLayer							renderLayer;
} mwdView;

typedef struct mwdRenderData
{
	struct wlr_output		*output;
	struct wlr_renderer		*renderer;
	struct timespec			when;
	mwdView					*view;

	int						sx, sy;
} mwdRenderData;

typedef struct mwdKeyboard
{
	struct wl_list						link;
	mwdServer							*server;

	struct wlr_input_device				*device;

	struct wl_listener					modifiers;
	struct wl_listener					key;
} mwdKeyboard;

typedef struct mwdViewInterface
{
	struct {
		void				(*pos			)(mwdView *view, double top, double right, double bottom, double left);
		void				(*activated		)(mwdView *view, bool activated);
	} set;

	struct {
		struct wlr_surface	*(*surface		)(mwdView *view);
		bool				(*constraints	)(mwdView *view, double *minWidth, double *maxWidth, double *minHeight, double *maxHeight);
		void				(*pos			)(mwdView *view, double *top, double *right, double *bottom, double *left);
		bool				(*activated		)(mwdView *view);
	} get;

	struct {
		bool				(*valid			)(mwdView *view);
		bool				(*at			)(mwdView *view, double x, double y, struct wlr_surface **surface, double *offsetX, double *offsetY);
		bool				(*visible		)(mwdView *view, mwdOutput *output);
	} is;

	struct {
		void				(*surface		)(mwdView *view, wlr_surface_iterator_func_t iterator, void *user_data);
	} foreach;

    void					(*destroy		)(mwdView *view);
    void					(*render		)(mwdView *view, struct wlr_renderer *renderer, mwdOutput *output);
} mwdViewInterface;

/* output.c */
void OutputAdd(struct wl_listener *listener, void *data);
void OutputApplyCfg(struct wl_listener *listener, void *data);
void OutputLayoutChanged(struct wl_listener *listener, void *data);
void OutputTestCfg(struct wl_listener *listener, void *data);
mwdOutput *OutputFind(mwdServer *server, struct wlr_output *output);
void OutputTestApply(struct mwdOutputTest *test);
void OutputTestRevert(struct mwdOutputTest *test);

/* render.c */
void RenderFrame(struct wl_listener *listener, void *data);
void RenderSurface(struct wlr_surface *surface, int sx, int sy, void *data);
void RenderPopupSurface(struct wlr_surface *surface, int sx, int sy, void *data);

/* input.c */
void inputMain(mwdServer *server);

/* view.c */
void RenderView(mwdView *view, struct wlr_renderer *renderer, mwdOutput *output);
mwdView *CreateNewView(mwdServer *server);
bool ViewIsValid(mwdView *view);
bool ViewIsVisible(mwdView *view, mwdOutput *output);
struct wlr_surface *ViewGetSurface(mwdView *view);
bool ViewIsFocused(mwdView *view);
void ViewFocus(mwdView *view, bool raise);
mwdView *ViewFocused(mwdServer *server);
mwdView *ViewNext(mwdView *view);
mwdView *ViewPrev(mwdView *view);
void ViewGrab(mwdView *view, mwdGrabMode mode, uint32_t edges);

mwdView *ViewFindByPos(mwdServer *server, double x, double y, struct wlr_surface **psurface, double *offsetX, double *offsetY);
mwdView *ViewFindBySurface(mwdServer *server, struct wlr_surface *surface);
void ViewSetActivated(mwdView *view, bool activated);
bool ViewGetConstraints(mwdView *view, double *minWidth, double *maxWidth, double *minHeight, double *maxHeight);
void ViewSetPos(mwdView *view, double top, double right, double bottom, double left);
void ViewGetPos(mwdView *view, double *top, double *right, double *bottom, double *left);
void ViewGetSize(mwdView *view, double *width, double *height);

void ViewForEachSurface(mwdView *view, wlr_surface_iterator_func_t iterator, void *user_data);

/* shell_*.c */
void XdgMain(mwdServer *server);
void LayerMain(mwdServer *server);
void XWaylandMain(mwdServer *server);

#endif // _MWD_H


#include "../mwd.h"

/*
	Input handling:
		https://drewdevault.com/2018/07/17/Input-handling-in-wlroots.html
*/

static void handleCursorMove(mwdServer *server, uint32_t time)
{
	/* Move the grabbed view to the new position */
	mwdView	*view	= server->grab.view;

	/*
		Calculate the distance from the initial position that the cursor has
		moved.
	*/
	double			x		= server->cursor->x - server->grab.cursor.x;
	double			y		= server->cursor->y - server->grab.cursor.y;

	ViewSetPos(view, server->grab.top + y, server->grab.right + x, server->grab.bottom + y, server->grab.left + x);
}

static void constrainViewSize(mwdView *view, uint32_t edges, double *top, double *right, double *bottom, double *left)
{
	double	width	= *right - *left;
	double	height	= *bottom - *top;
	double	minWidth = 0, minHeight = 0;
	double	maxWidth = 0, maxHeight = 0;
	double	v;

	/* Apply the constraints the view requested */
	ViewGetConstraints(view, &minWidth, &maxWidth, &minHeight, &maxHeight);

	if (minWidth != 0 && width < minWidth) {
		if (edges & WLR_EDGE_LEFT) {
			*left = *right - minWidth;
		} else if (edges & WLR_EDGE_RIGHT) {
			*right = *left + minWidth;
		}
	}

	if (maxWidth != 0 && width > maxWidth) {
		if (edges & WLR_EDGE_LEFT) {
			*left = *right - maxWidth;
		} else if (edges & WLR_EDGE_RIGHT) {
			*right = *left + maxWidth;
		}
	}

	if (minHeight != 0 && height < minHeight) {
		if (edges & WLR_EDGE_TOP) {
			*top = *bottom - minHeight;
		} else if (edges & WLR_EDGE_BOTTOM) {
			*bottom = *top + minHeight;
		}
	}

	if (maxHeight != 0 && height > maxHeight) {
		if (edges & WLR_EDGE_TOP) {
			*top = *bottom - maxHeight;
		} else if (edges & WLR_EDGE_BOTTOM) {
			*bottom = *top + minHeight;
		}
	}
}

static void handleCursorResize(mwdServer *server, uint32_t time)
{
	mwdView	*view	= server->grab.view;

	/*
		Calculate the distance from the initial position that the cursor has
		moved.
	*/
	double	x		= server->cursor->x - server->grab.cursor.x;
	double	y		= server->cursor->y - server->grab.cursor.y;

	/* The edges of the box */
	double	top, right, bottom, left;

	/* Set the values as they were when the resize was started */
	top		= server->grab.top;
	right	= server->grab.right;
	bottom	= server->grab.bottom;
	left	= server->grab.left;

	if (server->grab.edges & WLR_EDGE_TOP) {
		top += y;
	} else if (server->grab.edges & WLR_EDGE_BOTTOM) {
		bottom += y;
	}

	if (server->grab.edges & WLR_EDGE_LEFT) {
		left += x;
	} else if (server->grab.edges & WLR_EDGE_RIGHT) {
		right += x;
	}

	/* Apply sanity checks */
	constrainViewSize(view, server->grab.edges, &top, &right, &bottom, &left);

	/*
		A new size has been calculated, but we don't actually want to move the
		view.

		Instead ask the view to resize, and set the edges that we are using to
		calculate the rendering position to be opposite the edges being used to
		resize.

		This should ensure that the corner opposite the one being dragged does
		not move at all. The actual rendering will get the actual applied size
		and position it properly based on those values and the specified edge.
	*/
	ViewSetPos(view, top, right, bottom, left);
	view->edges = (~server->grab.edges) & (WLR_EDGE_TOP | WLR_EDGE_RIGHT | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT);
}

static void handleCursorPassthrough(mwdServer *server, uint32_t time)
{
	/* Find the view under the pointer and send the event along. */
	double				sx, sy;
	struct wlr_seat		*seat		= server->seat;
	struct wlr_surface	*surface	= NULL;
	mwdView				*view		= ViewFindByPos(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);

	if (!view) {
		wlr_xcursor_manager_set_cursor_image(server->cursorMgr, "left_ptr", server->cursor);
	}

	if (!surface) {
		wlr_seat_pointer_clear_focus(seat);
	} else if (seat->pointer_state.focused_surface != surface) {
		wlr_seat_pointer_notify_enter(seat, surface, sx, sy);

		// TODO Give the user the choice to do sloppy focus or not. For now it
		//		is hardcoded by calling this, and we do NOT raise the window
		//		because that makes popups unusable with sloppy focus.
		ViewFocus(view, false);
	} else {
		wlr_seat_pointer_notify_motion(seat, time, sx, sy);
	}
}

static void handleCursorMotion(mwdServer *server, uint32_t time)
{
	/* If the mode is non-passthrough, delegate to those functions. */
	switch (server->grab.mode) {
		case MWD_GRAB_MOVE:
			handleCursorMove(server, time);
			break;

		case MWD_GRAB_RESIZE:
			handleCursorResize(server, time);
			break;

		default:
		case MWD_GRAB_NONE:
			handleCursorPassthrough(server, time);
			break;
	}
}

static void cursorMotionRelative(struct wl_listener *listener, void *data)
{
	mwdServer							*server	= wl_container_of(listener, server, cursorMotionRelative);
	struct wlr_event_pointer_motion		*event	= data;

	wlr_cursor_move(server->cursor, event->device, event->delta_x, event->delta_y);
	handleCursorMotion(server, event->time_msec);
}

static void cursorMotionAbsolute(struct wl_listener *listener, void *data)
{
	mwdServer									*server	= wl_container_of(listener, server, cursorMotionAbsolute);
	struct wlr_event_pointer_motion_absolute	*event	= data;

	wlr_cursor_warp_absolute(server->cursor, event->device, event->x, event->y);
	handleCursorMotion(server, event->time_msec);
}

static void cursorButton(struct wl_listener *listener, void *data)
{
	mwdServer							*server	= wl_container_of(listener, server, cursorButton);
	struct wlr_event_pointer_button		*event	= data;
	mwdView								*view;
	double								sx, sy;
	struct wlr_surface					*surface;

	if (event->state == WLR_BUTTON_RELEASED) {
		/* If you released any buttons, we exit interactive move/resize mode. */
		server->grab.mode = MWD_GRAB_NONE;
	} else {
		/* Focus that client if the button was _pressed_ */
		if ((view = ViewFindByPos(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy))) {
			ViewFocus(view, true);

			/* Drag or resize if a modifier is being used */
			if (server->modifiers & (WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO)) {
				switch (event->button) {
					case BTN_LEFT:
						ViewGrab(view, MWD_GRAB_MOVE, view->edges);
						return;

					case BTN_RIGHT:
						ViewGrab(view, MWD_GRAB_RESIZE, WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT);
						return;
				}
			}
		}
	}

	/* Notify the client with pointer focus that a button press has occurred */
	wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
}

static void cursorAxis(struct wl_listener *listener, void *data)
{
	/* Axis event (ie scroll wheel) */
	mwdServer						*server = wl_container_of(listener, server, cursorAxis);
	struct wlr_event_pointer_axis	*event = data;

	/* Notify the client with pointer focus of the axis event. */
	wlr_seat_pointer_notify_axis(server->seat, event->time_msec, event->orientation, event->delta, event->delta_discrete, event->source);
}

static void cursorFrame(struct wl_listener *listener, void *data)
{
	mwdServer		*server = wl_container_of(listener, server, cursorFrame);

	/* Notify the client with pointer focus of the frame event. */
	wlr_seat_pointer_notify_frame(server->seat);
}

static void kbdHandleModifiers(struct wl_listener *listener, void *data)
{
	mwdKeyboard		*keyboard = wl_container_of(listener, keyboard, modifiers);
	mwdServer		*server		= keyboard->server;

	wlr_seat_set_keyboard(server->seat, keyboard->device);

	/* Send modifiers to the client */
	wlr_seat_keyboard_notify_modifiers(server->seat, &keyboard->device->keyboard->modifiers);

	server->modifiers = wlr_keyboard_get_modifiers(keyboard->device->keyboard);
	// wlr_log(WLR_INFO, "modifiers: %08x", server->modifiers);
}

/* A key has been pressed or released */
static void kbdHandleKey(struct wl_listener *listener, void *data)
{
	mwdKeyboard						*keyboard	= wl_container_of(listener, keyboard, key);
	mwdServer						*server		= keyboard->server;
	struct wlr_event_keyboard_key	*event		= data;
	const xkb_keysym_t				*syms;
	int								nsyms;
	uint32_t						keycode;
	uint32_t						modifiers;

	/* Translate libinput keycode -> xkbcommon */
	keycode = event->keycode + 8;

	/* Get a list of keysyms based on the keymap for this keyboard */
	nsyms = xkb_state_key_get_syms(keyboard->device->keyboard->xkb_state, keycode, &syms);

	modifiers = wlr_keyboard_get_modifiers(keyboard->device->keyboard);

	// TODO Compare this event to a configured list of keybindings that the user
	//		has provided.

	// TODO Currently this assumes that anything with alt or super is for the
	//		compositor and anything else should be passed through.
	if (modifiers & (WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO)) {
		switch (event->state) {
			case WL_KEYBOARD_KEY_STATE_PRESSED:
				for (int i = 0; i < nsyms; i++) {
					switch (syms[i]) {
						case XKB_KEY_Escape:
							wl_display_terminate(server->display);
							return;

						case XKB_KEY_j:
							ViewFocus(ViewNext(ViewFocused(server)), true);
							return;

						case XKB_KEY_k:
							ViewFocus(ViewPrev(ViewFocused(server)), true);
							return;


						default:
							break;
					}
				}
				break;

			case WL_KEYBOARD_KEY_STATE_RELEASED:
				/* Cancel any grab that was in progress */
				server->grab.mode = MWD_GRAB_NONE;
				break;
		}
	}

	/* Otherwise, we pass it along to the client. */
	wlr_seat_set_keyboard(server->seat, keyboard->device);
	wlr_seat_keyboard_notify_key(server->seat, event->time_msec, event->keycode, event->state);
}

static void newKeyboard(mwdServer *server, struct wlr_input_device *device)
{
	mwdKeyboard				*keyboard;
	struct xkb_rule_names	rules	= { 0 };
	struct xkb_context		*context;
	struct xkb_keymap		*keymap;

	if (!(keyboard = calloc(1, sizeof(mwdKeyboard)))) {
		return;
	}

	keyboard->server	= server;
	keyboard->device	= device;

	/* Prepare an XKB keymap and assign it to the keyboard */
	// TODO Let a user configure the layout

	context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	keymap = xkb_map_new_from_names(context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);

	wlr_keyboard_set_keymap(device->keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	wlr_keyboard_set_repeat_info(device->keyboard, 25, 600);

	/* Here we set up listeners for keyboard events. */
	keyboard->modifiers.notify = kbdHandleModifiers;
	wl_signal_add(&device->keyboard->events.modifiers, &keyboard->modifiers);

	keyboard->key.notify = kbdHandleKey;
	wl_signal_add(&device->keyboard->events.key, &keyboard->key);

	wlr_seat_set_keyboard(server->seat, device);

	/* And add the keyboard to our list of keyboards */
	wl_list_insert(&server->keyboards, &keyboard->link);
}

static void newPointer(mwdServer *server, struct wlr_input_device *device)
{
	wlr_cursor_attach_input_device(server->cursor, device);
}

static void newInput(struct wl_listener *listener, void *data)
{
	mwdServer					*server	= wl_container_of(listener, server, newInput);
	struct wlr_input_device		*device	= data;
	uint32_t					caps;

	switch (device->type) {
		case WLR_INPUT_DEVICE_KEYBOARD:
			newKeyboard(server, device);
			break;

		case WLR_INPUT_DEVICE_POINTER:
			newPointer(server, device);
			break;

		default:
			break;
	}

	/* Inform the wlr_seat of our capabilities */
	caps = WL_SEAT_CAPABILITY_POINTER;

	if (!wl_list_empty(&server->keyboards)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(server->seat, caps);
}

/* This event is raised by the seat when a client provides a cursor image */
static void requestCursor(struct wl_listener *listener, void *data)
{
	mwdServer											*server	= wl_container_of(listener, server, requestCursor);
	struct wlr_seat_pointer_request_set_cursor_event	*event	= data;

	if (server->seat->pointer_state.focused_client != event->seat_client) {
		return;
	}

	wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
}

void inputMain(mwdServer *server)
{
	wl_list_init(&server->keyboards);

	server->cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server->cursor, server->layout);

	server->cursorMgr = wlr_xcursor_manager_create(NULL, 24);
	wlr_xcursor_manager_load(server->cursorMgr, 1);

	server->cursorMotionRelative.notify	= cursorMotionRelative;
	server->cursorMotionAbsolute.notify	= cursorMotionAbsolute;
	server->cursorButton.notify			= cursorButton;
	server->cursorAxis.notify			= cursorAxis;
	server->cursorFrame.notify			= cursorFrame;
	server->newInput.notify				= newInput;
	server->requestCursor.notify		= requestCursor;

	wl_signal_add(&server->cursor->events.motion,			&server->cursorMotionRelative);
	wl_signal_add(&server->cursor->events.motion_absolute,	&server->cursorMotionAbsolute);
	wl_signal_add(&server->cursor->events.button,			&server->cursorButton);
	wl_signal_add(&server->cursor->events.axis,				&server->cursorAxis);
	wl_signal_add(&server->cursor->events.frame,			&server->cursorFrame);
	wl_signal_add(&server->backend->events.new_input,		&server->newInput);

	/* Configure a single "seat" and listen for new input devices */
	server->seat = wlr_seat_create(server->display, "seat0");

	wl_signal_add(&server->seat->events.request_set_cursor,	&server->requestCursor);
}


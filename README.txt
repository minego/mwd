Manage Wayland Dynamically

		or

My Wayland Doohicky

		or

Make Wayland Dynamic
================================================================================

This project is an attempt at implementing a dynamic tiling compositor for
wayland.

TODO
================================================================================

	* Run an init script similar to cardboard or river
	* Sloppy focus

	- popups
	- Tray
	- Fullscreen
	- XWayland

	- Multiple output (Mostly there, but some things need to be moved or fixed)

	- mwdctl
		A command line utility that will control and configure mwd on the fly
		which can be called from the mwdrc script or from keybindinds.

		Commands:
			- exec <cmdline>
				Execute the specified command. Some mwd details will be
				available in the environment of the child process.

			- bind <key> <mwdctl command>
				Bind a keystroke to a specific mwdctl command. Examples:
					# Bind a keystroke to start a terminal
					mwctl bind mod1+shift+enter exec kitty

					# Bind keystrokes to change the selected window
					mwdctl bind mod1+j next_view
					mwdctl bind mod1+k prev_view

			- set <name> <value>
				Set a user preference such as background_color or border_width

			- get <name>
				Return the value of a user preference

			- dump
				Print all currently set options, bindings, rules, etc as mwdctl
				commands in a form that is suitable for inclusion in an mwdrc
				file.

	- Rules
		Allow setting rules that apply to specific applications, and configure
		the behavior such as the default tags to be on.

	- Per application keyboard remapping
		Allow a rule to specify keyboard remapping that should be done on the
		fly for a specific application. For example a user may want super+c in a
		terminal to be mapped to control+shift+c but in other application it
		should be control+c.

	- Tags (dwm style)

	- Basic tiling layouts
		- dwm style
		- m-column
		- paperwm style
		- Allow use of scripts for layout?
		- Implement the ` tag, which behaves much like a tag, but all views on
		that tag are shown in their own column on the left. This is intended for
		things like chat clients that a user may want to always have visible in
		a smaller view.

	- Constrain size based on tile layout? If a window was asked to resize but
	hasn't yet (or refuses to) then should it be cropped?

	- Verify that things like steam work
	- Implement all possible extensions

	- Nice to have features
		- Mouse warping
		* Resize while tiling with mouse
		- Zoom out option, basically expose, show all tags
		- Support for wlr-randr
		- Support for wlr-dpms
		- HiDPI


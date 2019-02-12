#!/usr/bin/env python

import sys
from gnuradio import gr, usrp
from gnuradio.eng_option import eng_option
from optparse import OptionParser
import omnipod
import wx
import os


# assign an id to event types
ID_DATA_AVAILABLE = wx.NewId()
ID_STATUS_AVAILABLE = wx.NewId()

# assign an id to the event that handles the above types
ID_AVAILABLE_EVENT = wx.NewId()

# global containing window to receive "available" events
g_win = None


# the event that holds our "available" types
#
# The main reason this is adventageous is that these events
# are queued up in an arbitrary length list and delivered
# in a FIFO.  (I'm just guessing here, but this is logical.)
#
# Alternatively, you'll get one of these messages delivered,
# since they are running in different threads, while the
# current message is being processed.  You can handle this
# with a mutex, but this ends up being (slightly) easier.

class available_event(wx.PyEvent):
	def __init__(self, event_type, data):
		wx.PyEvent.__init__(self)
		self.SetEventType(ID_AVAILABLE_EVENT)
		self.event_type = event_type
		self.data = data


def valid_rx_subdev(u, s):
	if((u.db(s[0], s[1]).dbid() == 1) or (u.db(s[0], s[1]).dbid() == 15)):
		return True
	return False


def valid_tx_subdev(u, s):
	if((u.db(s[0], s[1]).dbid() == 0) or (u.db(s[0], s[1]).dbid() == 14)):
		return True
	return False


def pick_rx_subdev_spec(u):
	if(valid_rx_subdev(u, (0, 0))):
		return (0, 0)
	if(valid_rx_subdev(u, (1, 0))):
		return (1, 0)
	print "No suitable RX daughterboard found!"
	sys.exit(-1)


def pick_tx_subdev_spec(u):
	if(valid_tx_subdev(u, (0, 0))):
		return (0, 0)
	if(valid_tx_subdev(u, (1, 0))):
		return (1, 0)
	print "No suitable TX daughterboard found!"
	sys.exit(-1)
	 

class interface_director(omnipod.interface_director):
	def __init__(self):
		omnipod.interface_director.__init__(self)

	def display_data(self, data):
		if g_win is not None:
			wx.PostEvent(g_win, available_event(ID_DATA_AVAILABLE, data))

	def display_status(self, data):
		if g_win is not None:
			wx.PostEvent(g_win, available_event(ID_STATUS_AVAILABLE, data))


class transceiver_interface(gr.top_block):
	def __init__(self, idirector):
		gr.top_block.__init__(self)

		parser = OptionParser(option_class = eng_option)
		parser.add_option("-f", "--filename", type = "string", default = None,
		   help = "use a file as input rather than the USRP")
		parser.add_option("-r", "--replay-filename", type = "string", default = None,
		   help = "use a file to replay TX")
		parser.add_option("-w", "--which", type = "int", default = 0,
		   help = "select which USRP (default is %default)")
		parser.add_option("-R", "--rx-subdev-spec", type = "subdev", default = None,
		   help = "select USRP RX side A or B")
		parser.add_option("-T", "--tx-subdev-spec", type = "subdev", default = None,
		   help = "select USRP TX side A or B")
		(options, args) = parser.parse_args()

		# do we still have arguments left over?
		if len(args) != 0:
			parser.print_help()
			sys.exit(1)

		# XXX set to 20MHz for testing
		# self.transceiver_freq = 20e6
		self.transceiver_freq = 13.56e6

		# XXX This crashes glibc when it creates new threads.  It
		# shouldn't be necessary anyway.
		#
		# r = gr.enable_realtime_scheduling()
		# if r != gr.RT_OK:
		# 	print "error: failed to enable realtime scheduling"
		# 	sys.exit(-1)

		sample_rate = 250000.0

		if options.filename is not None:
			self.source = gr.file_source(gr.sizeof_gr_complex, options.filename, 0)
			self.sink = gr.null_sink(gr.sizeof_gr_complex)
		else:
			try:
				# self.source = usrp.source_c(which = options.which, fusb_block_size = 4096, fusb_nblocks = 4)
				self.source = usrp.source_c(which = options.which, fusb_block_size = 512)
				# self.sink = usrp.sink_c(which = options.which, fusb_block_size = 1024, fusb_nblocks = 8)
				self.sink = usrp.sink_c(which = options.which, fusb_block_size = 512)
			except RuntimeError:
				print "error: cannot open USRP"
				sys.exit(-1)
	
			# note this works for 52MHz and 64MHz clocks, not sure about others
			decimation = int(self.source.adc_rate() / sample_rate)
			interpolation = 2 * decimation
			self.source.set_decim_rate(decimation)
			self.sink.set_interp_rate(interpolation)
		
			sample_rate = self.source.adc_rate() / decimation
			if sample_rate != self.sink.dac_rate() / interpolation:
				print "error: decimation and interpolation not balanced"
				sys.exit(-1)
		
			if options.rx_subdev_spec is not None:
				if not valid_rx_subdev(options.rx_subdev_spec):
					print "Invalid RX daughterboard specified"
					sys.exit(-1)
				rx_subdev_spec = options.rx_subdev_spec
			else:
				rx_subdev_spec = pick_rx_subdev_spec(self.source)
			rx_subdev = usrp.selected_subdev(self.source, rx_subdev_spec)
			
			if options.tx_subdev_spec is not None:
				if not valid_tx_subdev(options.tx_subdev_spec):
					print "Invalid TX daughterboard specified"
					sys.exit(-1)
				tx_subdev_spec = options.tx_subdev_spec
			else:
				tx_subdev_spec = pick_tx_subdev_spec(self.sink)
			tx_subdev = usrp.selected_subdev(self.sink, tx_subdev_spec)
		
			self.source.set_mux(usrp.determine_rx_mux_value(self.source, rx_subdev_spec))
			self.sink.set_mux(usrp.determine_tx_mux_value(self.sink, tx_subdev_spec))
		
			if not self.source.tune(0, rx_subdev, self.transceiver_freq):
				print "Failed to set RX frequency"
				sys.exit(-1)
			if not self.sink.tune(0, tx_subdev, self.transceiver_freq):
				print "Failed to set TX frequency"
				sys.exit(-1)
	
			rx_gain_range = rx_subdev.gain_range()
			rx_subdev.set_gain(0.75 * (rx_gain_range[1] - rx_gain_range[0]) + rx_gain_range[0])
			tx_subdev.set_gain(tx_subdev.gain_range()[1])
	
		self.idirector = idirector
		self.transceiver = omnipod.pda(sample_rate, idirector)

		if options.replay_filename is not None:
			throttle = gr.throttle(gr.sizeof_gr_complex, sample_rate);
			fsource = gr.file_source(gr.sizeof_gr_complex, options.replay_filename, 0);
			nsink = gr.null_sink(gr.sizeof_gr_complex)
			self.connect(fsource, throttle, self.sink)
			self.connect(self.source, self.transceiver, nsink)
		else:
			self.connect(self.source, self.transceiver, self.sink)


	def __del__(self):
		self.stop()

	def do_start(self):
		self.start()
		self.idirector.display_status("PDA Transceiver started")

	def do_stop(self):
		self.stop()
		self.idirector.display_status("PDA Transceiver stopped")

	def set_monitor(self, on):
		self.transceiver.set_monitor(on)

	def start_status(self):
		self.transceiver.start_status()

	def set_secret(self, secret):
		self.transceiver.set_secret(secret)

	def set_seqno(self, seqno):
		self.transceiver.set_seqno(seqno)


class pda_ui(wx.Frame):
	def __init__(self, parent, title, tinterface):
		wx.Frame.__init__(self, parent, title = title)

		# event handler for "available" messages
		self.Connect(-1, -1, ID_AVAILABLE_EVENT, self.available_message)

		# Save a copy of the PDA transceiver class
		self.tinterface = tinterface

		self.CreateStatusBar()

		# menus
		filemenu = wx.Menu()
		filemenu.AppendSeparator()
		menu_save = filemenu.Append(wx.ID_SAVEAS, "&Save\tCtrl+S", "Save Data")
		menu_exit = filemenu.Append(wx.ID_EXIT, "E&xit\tCtrl+Q", "Terminate OmniHack")

		helpmenu = wx.Menu()
		menu_about = helpmenu.Append(wx.ID_ABOUT, "&About", "About OmniHack")

		# create a menu bar and add the menus
		menu_bar = wx.MenuBar()
		menu_bar.Append(filemenu, "&File")
		menu_bar.Append(helpmenu, "Help")
		self.SetMenuBar(menu_bar)

		# construct a simple layout
		vsizer = wx.BoxSizer(wx.VERTICAL)
		hsizer = wx.BoxSizer(wx.HORIZONTAL)

		# Monitor Mode
		monitor_checkbox = wx.CheckBox(self, label = "Monitor Mode")
		monitor_checkbox.SetValue(True)
		self.tinterface.set_monitor(True)

		# Start / Stop
		self.run_button = wx.ToggleButton(self, -1, label = "Run")

		# Clear text
		self.clear_button = wx.Button(self, -1, label = "Clear")

		# Status Protocol start / stop
		status_button = wx.Button(self, -1, label = "Status")

		# Secret Value
		secret_text = wx.StaticText(self, label = "32-bit Secret")
		self.secret_ctrl = wx.TextCtrl(self, value = "", style = wx.TE_PROCESS_ENTER)
		self.secret_ctrl.SetMaxLength(8)
		self.secret_ctrl.SetValue("c504d891");
		
		# Sequence Value
		seqno_text = wx.StaticText(self, label = "8-bit Sequence Number")
		self.seqno_ctrl = wx.TextCtrl(self, value = "", style = wx.TE_PROCESS_ENTER)
		self.seqno_ctrl.SetMaxLength(2)
		self.seqno_ctrl.SetValue("00");

		# Create a text area for data
		# self.logger = wx.TextCtrl(self, style = wx.TE_MULTILINE | wx.TE_READONLY | wx.HSCROLL, size = (800, 600))
		self.logger = wx.TextCtrl(self, style = wx.TE_MULTILINE | wx.HSCROLL, size = (800, 600))
		self.logger.SetFont(wx.Font(10, wx.TELETYPE, -1, -1))

		# Main screen is columns
		hsizer.Add(self.logger, 1, wx.EXPAND | wx.RIGHT, 5)
		hsizer.Add(vsizer, 0, wx.EXPAND)

		# Rows in second column
		vsizer.Add(self.run_button, 0, wx.ALIGN_LEFT | wx.BOTTOM, 10)
		vsizer.Add(self.clear_button, 0, wx.ALIGN_LEFT | wx.BOTTOM, 30)
		vsizer.Add(monitor_checkbox, 0, wx.BOTTOM | wx.ALIGN_LEFT | wx.RIGHT, 10)
		vsizer.Add(status_button, 0, wx.ALIGN_LEFT | wx.BOTTOM, 30)
		vsizer.Add(secret_text, 0, wx.ALIGN_LEFT)
		vsizer.Add(self.secret_ctrl, 0, wx.BOTTOM | wx.RIGHT | wx.ALIGN_LEFT, 10)
		vsizer.Add(seqno_text, 0, wx.ALIGN_LEFT | wx.RIGHT, 10)
		vsizer.Add(self.seqno_ctrl, 0, wx.ALIGN_LEFT | wx.RIGHT, 10)

		self.SetSizer(hsizer)
		self.SetAutoLayout(1)
		hsizer.Fit(self)
		
		# Bind events
		self.Bind(wx.EVT_MENU, self.on_save, menu_save)
		self.Bind(wx.EVT_MENU, self.on_exit, menu_exit)
		self.Bind(wx.EVT_MENU, self.on_about, menu_about)

		self.Bind(wx.EVT_TOGGLEBUTTON, self.run_pressed, self.run_button)
		self.Bind(wx.EVT_BUTTON, self.clear_pressed, self.clear_button)
		self.Bind(wx.EVT_CHECKBOX, self.monitor_checked, monitor_checkbox)
		self.Bind(wx.EVT_BUTTON, self.pod_status_pressed, status_button)
		self.Bind(wx.EVT_TEXT_ENTER, self.set_secret, self.secret_ctrl)
		self.Bind(wx.EVT_TEXT_ENTER, self.set_seqno, self.seqno_ctrl)

		self.Show(True)

		# save global so events can be sent to this window
		global g_win
		g_win = self


	def on_save(self, e):
		content = self.logger.GetValue()
		if len(content) == 0:
			self.SetStatusText("There is no data to save!")
			return
		dlg = wx.FileDialog(self, style = wx.FD_SAVE | wx.FD_OVERWRITE_PROMPT | wx.FD_CHANGE_DIR)
		if dlg.ShowModal() == wx.ID_OK:
			filename = dlg.GetFilename()
			directory = dlg.GetDirectory()
			fullname = os.path.join(directory, filename)
			try:
				f = open(fullname, 'w')
			except IOError:
				self.SetStatusText("Error: cannot open %s for writing!" % fullname)
				dlg.Destroy()
				return
			f.write(content)
			f.close()
		dlg.Destroy()

	def on_exit(self, e):
		self.Close(True)

	def on_about(self, e):
		dlg = wx.MessageDialog(self, "Copyright 2011, Joshua Lackey\nAll rights reserved.", "OmniHack", wx.OK)
		dlg.ShowModal()
		dlg.Destroy()

	def run_pressed(self, e):
		if(e.Checked() == 1):
			self.tinterface.do_start()
			self.run_button.SetLabel("Stop")
		else:
			self.tinterface.do_stop()
			self.run_button.SetLabel("Run")
			self.tinterface.wait() # must do after a stop

	def clear_pressed(self, e):
		self.logger.Clear()

	def pod_status_pressed(self, e):
		secret = self.secret_ctrl.GetValue()
		seqno = self.seqno_ctrl.GetValue()
		if not self.validate_secret():
			return
		if not self.validate_seqno():
			return
		secret_int = int(secret, 16)
		seqno_int = int(seqno, 16)
		self.set_secret(secret)
		self.set_seqno(seqno)
		self.tinterface.start_status()

	def monitor_checked(self, e):
		self.tinterface.set_monitor(e.Checked())

	def validate_secret(self):
		ctrl = self.secret_ctrl
		try:
			int(ctrl.GetValue(), 16)
		except ValueError:
			self.SetStatusText("Non-hex characters in secret!")
			ctrl.SetBackgroundColour("pink")
			ctrl.SetFocus()
			ctrl.Refresh()
			return False
		ctrl.SetBackgroundColour(wx.SystemSettings_GetColour(wx.SYS_COLOUR_WINDOW))
		ctrl.Refresh()
		self.SetStatusText("")
		return True

	def validate_seqno(self):
		ctrl = self.seqno_ctrl
		try:
			int(ctrl.GetValue(), 16)
		except ValueError:
			self.SetStatusText("Non-hex characters in sequence number!")
			ctrl.SetBackgroundColour("pink")
			ctrl.SetFocus()
			ctrl.Refresh()
			return False
		ctrl.SetBackgroundColour(wx.SystemSettings_GetColour(wx.SYS_COLOUR_WINDOW))
		ctrl.Refresh()
		self.SetStatusText("")
		return True

	def set_secret(self, e):
		if self.validate_secret():
			self.tinterface.set_secret(int(self.secret_ctrl.GetValue(), 16))

	def set_seqno(self, e):
		if self.validate_seqno():
			self.tinterface.set_seqno(int(self.seqno_ctrl.GetValue(), 16))

	def available_message(self, e):
		if e.event_type == ID_DATA_AVAILABLE:
			self.display_data(e.data)
		elif e.event_type == ID_STATUS_AVAILABLE:
			self.display_status(e.data)
		else:
			self.SetStatusText("Unknown Available Event received")

	def display_data(self, d):
		self.logger.AppendText("%s\n" % d)

	def display_status(self, s):
		self.SetStatusText("%s" % s)


def main():
	idirector = interface_director()
	tinterface = transceiver_interface(idirector)

	wxapp = wx.App(redirect = 0)
	pda = pda_ui(None, "OmniHack", tinterface)
	wxapp.MainLoop()


if __name__ == '__main__':
	main()


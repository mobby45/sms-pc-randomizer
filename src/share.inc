// Included in the Randomizer class (randomizer.cpp): the data folder, and a game kept in one file.

	// What the player sees under the PLAY tab after Export or Import
	std::string shareNote_;
	double shareNoteAt_ = -100;

	std::string dataFolder() const { return core().data_dir(handle()); }

	// Opens the mod's data folder (the spoiler logs, plando.txt, exclusions.txt, share.txt) in the system's files,
	// and says where it is (on the new game screen, and in the log): the opener may be missing (Linux: xdg-open;
	// macOS: open), and the player can then go there by hand.
	void openDataFolder()
	{
		const std::string dir = dataFolder();
		info("data folder: %s", dir.c_str());
		shareNote_   = "DATA FOLDER: " + dir;
		shareNoteAt_ = core().time();
#ifdef _WIN32
		ShellExecuteA(nullptr, "open", dir.c_str(), nullptr, nullptr, 1);
#else
#ifdef __APPLE__
		static const char* const opener = "open";
#else
		static const char* const opener = "xdg-open";
#endif
		// no shell (a path with a quote is fine): a child that starts the opener and goes, so that nothing is left to wait for
		const pid_t child = fork();
		if (child == 0) {
			if (fork() == 0) {
				execlp(opener, opener, dir.c_str(), (char*)nullptr);
				_exit(127); // no opener
			}
			_exit(0);
		} else if (child > 0) {
			waitpid(child, nullptr, 0);
		} else {
			warn("cannot open %s", dir.c_str());
		}
#endif
	}

	static std::string readWhole(const std::string& path)
	{
		std::string text;
		FILE* f = fopen(path.c_str(), "rb");
		if (!f)
			return text;
		char buf[512];
		size_t n;
		while ((n = fread(buf, 1, sizeof buf, f)) > 0)
			text.append(buf, n);
		fclose(f);
		return text;
	}
	static void writeWhole(const std::string& path, const std::string& text)
	{
		FILE* f = fopen(path.c_str(), "wb");
		if (!f)
			return;
		fwrite(text.data(), 1, text.size(), f);
		fclose(f);
	}

	// The settings that make a game, kept with the plando and the exclusions in `share.txt`: to send to someone
	// who imports it, and plays the same game.
	static const char* const* shareKeys()
	{
		static const char* const k[] = { "seed", "win", "win_custom", "entrance_mode", "all_unlocks", "open_gates",
			                             "coin_shines", "trade_shines", "coin_unlocks", "shine_swaps" };
		return k;
	}
	static const int kShareKeys = 10;

	void exportGame()
	{
		std::string out = "# a randomizer game: import it from the new game screen (PLAY, Import game)\n[settings]\n";
		for (int k = 0; k < kShareKeys; ++k)
			out += std::string(shareKeys()[k]) + " = " + std::to_string(setting(shareKeys()[k], k == 2 ? 30 : 0)) + "\n";
		out += "[plando]\n" + readWhole(dataFolder() + "/plando.txt");
		out += "[exclusions]\n" + readWhole(dataFolder() + "/exclusions.txt");
		writeWhole(dataFolder() + "/share.txt", out);
		shareNote_   = "EXPORTED TO share.txt IN THE DATA FOLDER";
		shareNoteAt_ = core().time();
	}

	void importGame()
	{
		const std::string text = readWhole(dataFolder() + "/share.txt");
		if (text.empty()) {
			shareNote_   = "NO share.txt IN THE DATA FOLDER";
			shareNoteAt_ = core().time();
			return;
		}
		std::string section, plando, exclusions;
		size_t pos = 0;
		while (pos < text.size()) {
			size_t end = text.find('\n', pos);
			if (end == std::string::npos)
				end = text.size();
			std::string line = text.substr(pos, end - pos);
			pos              = end + 1;
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			if (line == "[settings]" || line == "[plando]" || line == "[exclusions]") {
				section = line;
				continue;
			}
			if (section == "[plando]") {
				plando += line + "\n";
			} else if (section == "[exclusions]") {
				exclusions += line + "\n";
			} else if (section == "[settings]") {
				const size_t eq = line.find('=');
				if (eq == std::string::npos)
					continue;
				std::string key = line.substr(0, eq);
				while (!key.empty() && key.back() == ' ')
					key.pop_back();
				for (int k = 0; k < kShareKeys; ++k) {
					if (key == shareKeys()[k])
						config().set_int(handle(), shareKeys()[k], atoi(line.c_str() + eq + 1));
				}
			}
		}
		config().save(handle());
		writeWhole(dataFolder() + "/plando.txt", plando);
		writeWhole(dataFolder() + "/exclusions.txt", exclusions);
		exclText_.clear(); // read again
		plandoText_.clear();
		loadExclusions();
		plDirty_     = true;
		exclView_.clear();
		shareNote_   = "IMPORTED share.txt";
		shareNoteAt_ = core().time();
	}

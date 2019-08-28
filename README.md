# Performance Goblin
A little goblin who gleefully adjusts quality settings to maximize our gaming experience.

(Okay, technically it's a "polynomial-time approximate multiple-choice knapsack optimization solver" which can be applied to many other purposes — but it's far too much work explaining all of those.)

![a drawing of a goblin](http://goblin.png)

By [Evan Balster](https://imitone.com)



## Problem Definition

The goblin solves a "Multiple-Choice Knapsack Problem" of the following form:

* We have a list of **decisions** to make.
* Each decision has a list of **options**, from which we choose exactly one.
* Each option defines a **burden** and a **value**.
* We have a **capacity** that limits the **total burden** of chosen options.
* We want to maximize **total value**.

Intuitively, we have a box of limited size, and we're trying to fit as much as possible.  Here's an example solution, where the width of a box represents burden and the area of the box represents value.  Note the small amount of unused space:

![a series of differently-shaped boxes, sorted by height and packed tightly into a tray.](http://example-solution.png)



## The Algorithm

Finding the highest-value choices is NP-hard, so this algorithm has a **precision** parameter.  The default precision of 20 allows for a solution whose value is up to 5% (1/20) lower than the best possible solution.

This algorithm runs in **O(N² × M × precision) **time, where N is the number of decisions and M is the mean number of options per decision.

The algorithm works by rounding each choice's value to an integer "score" between 0 and precision.  It then examines every set `0..i` up to `i = N`.  For each subset, it find the lowest-burden strategy for every possible total score and enters these into a table of size **N × max_score** (where the latter is the highest net score possible).  Each row in the table is based on the previous row.  Finally, we look at the complete set `0..N` and find the highest value for which the minimum burden does not exceed our capacity.

This algorithm is based on a commonly-used FPTAS algorithm for the traditional knapsack problem.



## Using the Goblin to Budget Processor Time

This goblin was designed for automating quality settings in games and multimedia applications in order to work within a CPU or GPU budget.  For example, a 60 FPS application has 16.7 milliseconds to process each frame, and this time might be allocated among various expensive tasks.

Therefore, our **capacity** is the available time (16.7 ms) and our **burden** is based on how much time is taken by various sub-tasks.  We might define **value** subjectively, based on how much each option brings to the experience.

### Determining Burden with Profiling

Ideally, our burden values should be based on live measurements of the time costs associated with our workload.  This allows the algorithm to account for unexpected changes in performance.

Because we can't measure the time costs associated with un-selected options, we'll need a way to estimate them based on current costs or past measurements.  For example, a post-processing effect might be expected to become about four times cheaper if we halve the width and height of the associated render-textures.  A particle field might take CPU and GPU time directly proportional to the number of particles.

### Estimating Burden without Profiling

If we can't accurately measure time costs in-game, we may use our own rough estimates for each option's relative burden.  We may then adjust our estimated capacity based on overall performance.

For example, when we drop below 60 frames per second, we should reduce our estimated capacity.  When we complete frames with time to spare, we may increase it.

### Binary Decisions with `add_binary_item`

Simple on/off settings are common.

`Problem::add_binary_item(burden, value)` produces this decision:
	 `{ (#0, $0), (#burden, $value) }`.

In the example image above, blue boxes represents binary burdens.

### Multiple-Choice Decisions with `add_decision`

The algorithm supports an arbitrary number of options per decision.  These may have arbitrary burdens and values — but it is advisable to avoid radical outliers in value as these may dominate other decisions.

`Problem::add_decision(options)` produces a decision based on an array of options.

### Fixed Burdens with `add_burden`

In most cases, our settings will only govern part of the overall burden, and some of the workload we're budgeting for is non-negotiable.  We can model this as a decision with only one option, having no value.

`Problem::add_burden(burden)` produces this (non-)decision:
	 `{ (#burden, $0) }`.

In the example image above, the red box represents a fixed burden.

### Fixed Values with `add_incentive`

We can affect the final net value of any solution by adding an item with no burden and fixed value.  This will not affect the algorithm's choices, but can be useful for implementing **strategies** (see below).

`Problem::add_incentive(value)` produces this (non-)decision:
	 `{ (#0, $value) }`.

### Preventing Flip-Flopping

The goblin chooses settings every frame and does not care about previous settings.  Lacking our guidance they may be prone to "flip-flopping" — switching options so frequently it detracts from the experience.

Fortunately, it's easy to discourage this behavior with incentives.  We can simply add to the value of the current option, or subtract from the potentially-disruptive options.  We can even automate our penalty so the option is more likely to change when it's least noticeable!

### Combining Related Settings

Some decisions may be interlinked, such as settings which are more valuable in combination.  Because the algorithm assumes all decisions are independent, these situations need special handling.  One simple approach is to combine all valid combinations of the settings into a single multiple-choice setting.

For example, if there are three tiers of shader quality and four options for resolution, we can unify them into a single render-quality with 12 alternatives representing the available combinations.  We can then estimate the burden for each option as **total_pixels × time_per_pixel** and independently assign value to each option.

### Strategies for Far-Reaching Settings

High-level decisions such as screen resolution may affect the **burden** and **value** associated with many other settings — making it impossible to track all the possible combinations.  The choice of framerate may even impact our **capacity**.

Because decisions are assumed independent in this algorithm, the simplest way to deal with these "super-settings" is to reformulate the *entire* problem once for every possible combination.  Each variant of the problem can have differences in capacity, burdens and values.  I refer to these variants as **strategies**.

For example, our game may have a strategy for 60 FPS and a strategy for 30 FPS.  30 FPS doubles our capacity for processing and rendering, but it will hurt the quality of any small, fast-moving elements in our scene.  Scenes comprised of large, slow-moving elements might look just fine at 30 FPS.  A scene with a fast-moving pattern won't.  If our application is in VR, it will need to run at 90 FPS when the user's head is moving, but may be able to trade framerate for rendering quality when the user's head is still.

To model this, we can create two copies of the problem.  Many of our decisions will be identical in burden and value.  Options related to framerate-sensitive effects may have different **value** in our 30 FPS and 60 FPS strategies.  If the user's head is moving quickly, we may use `add_incentive` to greatly increase the net **value** of our 90 FPS strategy.

In order to choose our framerate, we find an optimal solution for each strategy, then pick the solution (and strategy) whose net value is highest.
/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Tests that the rule view custom properties (css variables) jump to definition works
// as expected.

const TEST_URI = `
  <style type='text/css'>
  @property --my-registered-color {
    syntax: "<color>";
    inherits: true;
    initial-value: gold;
  }

  :root {
    --my-color-1: tomato;
    --my-color-2: cyan;
    --my-color-3: green;
  }

  @starting-style {
    #testid {
      --my-color-1: hotpink;
    }
  }

  #testid {
    color: var(--my-color-1);
    background-color: var(--my-registered-color);
    border-width: 1px;
    outline-width: var(--undefined);

    @starting-style {
      outline-color: var(--my-color-1, var(--my-color-2));
    }
  }

  h1 {
    background-color: linear-gradient(var(--my-color-1), var(--my-color-2, var(--my-color-3)));
  }

  h2 {
    --my-color-2: chartreuse;
    color: var(--my-color-1);
  }

  h2::after {
    --my-color-1: azure;
    content: "-";
    color: var(--my-color-1);
  }

  h2::before {
    --my-color-1: blueviolet;
    content: "+";
    color: var(--my-color-1, var(--my-color-2, var(--my-color-3)));
  }

  </style>
  <h1 id='testid'>Styled Node</h1>
  <h2>sub</h2>
`;

add_task(async function () {
  await addTab("data:text/html;charset=utf-8," + encodeURIComponent(TEST_URI));
  const { inspector, view } = await openRuleView();
  await selectNode("h1#testid", inspector);

  info("Check that the correct rules are visible");
  is(
    view.styleDocument.querySelectorAll(`.ruleview-rule`).length,
    7,
    "Should have 7 rules."
  );

  let rule = getRuleViewRuleEditor(view, 2).rule;
  is(rule.selectorText, "#testid", "Second rule is #testid.");

  info(
    "Check that property not using variable don't have a jump to definition button"
  );
  let variableButtonEls = getJumpToDefinitionButtonForDeclaration(rule, {
    "border-width": "1px",
  });
  is(
    variableButtonEls.length,
    0,
    "border-width property does not have custom properties and no variable jump to definition is displayed."
  );

  info(
    "Check that property using unset variable don't have a jump to definition button"
  );
  variableButtonEls = getJumpToDefinitionButtonForDeclaration(rule, {
    "outline-width": "var(--undefined)",
  });
  is(
    variableButtonEls.length,
    0,
    "outline-width property has an undefined custom properties, so no variable jump to definition is displayed."
  );

  info(
    "Check that there's a jump to definition button for `color: var(--my-color-1)`"
  );
  variableButtonEls = getJumpToDefinitionButtonForDeclaration(rule, {
    color: "var(--my-color-1)",
  });
  is(
    variableButtonEls.length,
    1,
    "color property has custom property and variable jump to definition is displayed."
  );

  info("Click the --my-color-1 jump to definition button");
  await highlightProperty(view, variableButtonEls[0], "--my-color-1", "tomato");

  info(
    "Check that there's a jump to definition button for `background-color var(--my-registered-color)`"
  );
  variableButtonEls = getJumpToDefinitionButtonForDeclaration(rule, {
    "background-color": "var(--my-registered-color)",
  });
  is(
    variableButtonEls.length,
    1,
    "background-color property has a registered custom property and variable jump to definition is displayed."
  );

  info("Click the --my-registered-color jump to definition button");
  // Collapse the `@property` section to check that it gets expanded when clicking on the
  // jump to definition button.
  const registerPropertyToggle = view.styleDocument.querySelector(
    `[aria-controls="registered-properties-container"]`
  );
  registerPropertyToggle.click();
  is(
    registerPropertyToggle.ariaExpanded,
    "false",
    "@property section is collapsed"
  );

  const onHighlightProperty = view.once("element-highlighted");
  variableButtonEls[0].click();
  const highlightedElement = await onHighlightProperty;
  is(
    highlightedElement.querySelector(".ruleview-registered-property-name")
      .innerText,
    "--my-registered-color",
    "The expected element was highlighted"
  );
  is(
    registerPropertyToggle.ariaExpanded,
    "true",
    "@property section is expanded after clicking on the jump to definition button"
  );

  info(
    "Check that there are multiple jump to definition buttons when using multiple variables"
  );
  rule = getRuleViewRuleEditor(view, 4).rule;
  is(rule.selectorText, "h1", "Fifth rule is h1.");
  variableButtonEls = getJumpToDefinitionButtonForDeclaration(rule, {
    "background-color":
      "linear-gradient(var(--my-color-1), var(--my-color-2, var(--my-color-3)))",
  });
  Assert.deepEqual(
    [...variableButtonEls].map(el => el.dataset.variableName),
    ["--my-color-1", "--my-color-2", "--my-color-3"]
  );

  info(`Click the "--my-color-2" variable jump to definition button`);
  await highlightProperty(view, variableButtonEls[1], "--my-color-2", "cyan");

  info(`Click the fallback "--my-color-3" variable jump to definition button`);
  await highlightProperty(view, variableButtonEls[2], "--my-color-3", "green");

  info("Check that we can jump in @starting-style rule`");
  rule = getRuleViewRuleEditor(view, 1).rule;
  ok(rule.isInStartingStyle(), "Got expected starting style rule");
  variableButtonEls = getJumpToDefinitionButtonForDeclaration(rule, {
    "outline-color": "var(--my-color-1, var(--my-color-2))",
  });

  Assert.deepEqual(
    [...variableButtonEls].map(el => el.dataset.variableName),
    ["--my-color-1", "--my-color-2"]
  );

  info(
    "Click the --my-color-1 jump to definition button in @starting-style rule"
  );
  await highlightProperty(
    view,
    variableButtonEls[0],
    "--my-color-1",
    "hotpink"
  );

  info(
    "Click the --my-color-2 jump to definition button in @starting-style rule"
  );
  await highlightProperty(view, variableButtonEls[1], "--my-color-2", "cyan");

  info("Check that jump to definition works well with pseudo elements");
  await selectNode("h2", inspector);

  info("Expand the pseudo element section");
  const pseudoElementToggle = view.styleDocument.querySelector(
    `[aria-controls="pseudo-elements-container"]`
  );
  // sanity check
  is(
    pseudoElementToggle.ariaExpanded,
    "false",
    "pseudo element section is collapsed at first"
  );
  pseudoElementToggle.click();
  is(
    pseudoElementToggle.ariaExpanded,
    "true",
    "pseudo element section is now expanded"
  );

  rule = getRuleViewRuleEditor(view, 1, 0).rule;
  is(rule.selectorText, "h2::after", "First rule is h2::after");

  await highlightProperty(
    view,
    getJumpToDefinitionButtonForDeclaration(rule, {
      color: "var(--my-color-1)",
    })[0],
    "--my-color-1",
    "azure"
  );

  rule = getRuleViewRuleEditor(view, 1, 1).rule;
  is(rule.selectorText, "h2::before", "First rule is h2::before");

  variableButtonEls = getJumpToDefinitionButtonForDeclaration(rule, {
    color: "var(--my-color-1, var(--my-color-2, var(--my-color-3)))",
  });
  await highlightProperty(
    view,
    variableButtonEls[0],
    "--my-color-1",
    // definition in h2::before
    "blueviolet"
  );
  await highlightProperty(
    view,
    variableButtonEls[1],
    "--my-color-2",
    // definition in h2
    "chartreuse"
  );
  await highlightProperty(
    view,
    variableButtonEls[2],
    "--my-color-3",
    // definition in :root
    "green"
  );

  rule = getRuleViewRuleEditor(view, 4).rule;
  is(rule.selectorText, "h2", "Got expected h2 rule");
  await highlightProperty(
    view,
    getJumpToDefinitionButtonForDeclaration(rule, {
      color: "var(--my-color-1)",
    })[0],
    "--my-color-1",
    // definition in :root
    "tomato"
  );
});

add_task(async function checkClearSearch() {
  await addTab(
    "data:text/html;charset=utf-8," +
      encodeURIComponent(`
        <style type='text/css'>
          :root {
            --my-color-1: tomato;
          }

          h1 {
            --my-unique-var: var(--my-color-1);
          }
        </style>
        <h1>Filter</h1>
  `)
  );

  const { inspector, view } = await openRuleView();
  await selectNode("h1", inspector);

  info("Check that search is cleared when clicking on the jump button");
  await setSearchFilter(view, "--my-unique-var");

  // check that the rule view is filtered as expected
  await checkRuleViewContent(view, [
    { selector: "element", declarations: [] },
    {
      selector: "h1",
      declarations: [{ name: "--my-unique-var", value: "var(--my-color-1)" }],
    },
  ]);
  const rule = getRuleViewRuleEditor(view, 1).rule;
  is(rule.selectorText, "h1", "Got expected rule");
  await highlightProperty(
    view,
    getJumpToDefinitionButtonForDeclaration(rule, {
      "--my-unique-var": "var(--my-color-1)",
    })[0],
    "--my-color-1",
    // definition in :root
    "tomato"
  );
  is(view.searchField.value, "", "Search input was cleared");

  // check that the rule view is no longer filtered
  await checkRuleViewContent(view, [
    { selector: "element", declarations: [] },
    {
      selector: "h1",
      declarations: [{ name: "--my-unique-var", value: "var(--my-color-1)" }],
    },
    {
      header: "Inherited from html",
    },
    {
      selector: ":root",
      declarations: [{ name: "--my-color-1", value: "tomato" }],
    },
  ]);
});

function getJumpToDefinitionButtonForDeclaration(rule, declaration) {
  const [[name, value]] = Object.entries(declaration);
  const textProp = rule.textProps.find(prop => {
    return prop.name === name && prop.value === value;
  });

  if (!textProp) {
    throw Error(`Declaration ${name}:${value} not found on rule`);
  }

  return textProp.editor.element.querySelectorAll(".ruleview-variable-link");
}

/**
 * Click on the provided jump to definition button and wait for the element-highlighted
 * event to be emitted.
 *
 * @param {RuleView} view
 * @param {Element} jumpToDefinitionButton
 * @param {String} expectedPropertyName: The name of the property that should be highlighted
 * @param {String} expectedPropertyValue: The value of the property that should be highlighted
 */
async function highlightProperty(
  view,
  jumpToDefinitionButton,
  expectedPropertyName,
  expectedPropertyValue
) {
  const onHighlightProperty = view.once("element-highlighted");
  jumpToDefinitionButton.click();
  const highlightedElement = await onHighlightProperty;
  is(
    highlightedElement.querySelector(".ruleview-propertyname").innerText,
    expectedPropertyName,
    "The expected element was highlighted"
  );
  is(
    highlightedElement.querySelector(".ruleview-propertyvalue").innerText,
    expectedPropertyValue,
    "The expected element was highlighted"
  );
}

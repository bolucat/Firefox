use-border-radius-tokens
========================

This rule checks that CSS declarations use border-radius design token variables
instead of hardcoded values. This ensures consistent border-radius usage across
the application and makes it easier to maintain design system consistency.

Examples of incorrect code for this rule:
-----------------------------------------

.. code-block:: css

    .custom-button {
      border-radius: 0.5rem;
    }

.. code-block:: css

    .card {
      border-radius: 8px;
    }

.. code-block:: css

    .avatar {
      border-radius: 50%;
    }

Examples of correct token usage for this rule:
----------------------------------------------

.. code-block:: css

    .custom-button {
      border-radius: var(--border-radius-small);
    }

.. code-block:: css

    .custom-button {
      border-radius: var(--border-radius-medium);
    }

.. code-block:: css

    .custom-button {
      border-radius: var(--border-radius-circle);
    }

.. code-block:: css

    .custom-button {
      border-radius: var(--border-radius-large);
    }

.. code-block:: css

    /* Local CSS variables that reference valid border-radius tokens are allowed */
    :root {
      --custom-border-radius: var(--border-radius-small);
    }

    .custom-button {
      border-radius: var(--custom-border-radius);
    }

.. code-block:: css

    .custom-button {
      border-radius: var(--custom-border-radius, --border-radius-small);
    }



The rule also allows these values non-token values:

.. code-block:: css

    .no-radius {
      border-radius: 0;
    }

.. code-block:: css

    .inherited-radius {
      border-radius: inherit;
    }

.. code-block:: css

    .unset-radius {
      border-radius: unset;
    }

.. code-block:: css

    .initial-radius {
      border-radius: initial;
    }
